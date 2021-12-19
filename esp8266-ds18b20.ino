//General ESP8266 WiFi staff
#include <ESP8266WiFi.h>
#include <WiFiManager.h>

//network time needed
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <TimeLib.h>

//1-wire and dalas termo-sensors needed
#include <OneWire.h>
#include <DallasTemperature.h>

//Firebase related
#include <FirebaseESP8266.h>
#include <addons/TokenHelper.h>

//personal passwords predefined
#include "secret.h"
//or override below
#ifndef SECRET_H
#define WM_PASSWORD "123456"
#define API_KEY "fire base api key"
#define USER_EMAIL "user@server.com"
#define USER_PASSWORD "123456"
#define DATABASE_URL "https://database name.firebaseio.com" 
#endif


/********* NTP staff **************/
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 7200 /* timezone*/, 300000 /*refresh period in milliseconds*/);

/********* BS18B20 1-wire **************/
// OneWire датчики BS18B20 підключено до аналогового виводу A0 Arduino (14)
#define ONE_WIRE_BUS 2
// налаштування oneWire для звязку з датчиком температури
OneWire oneWire(ONE_WIRE_BUS);
// передаєм вказівник на oneWire в бібліотеку DallasTemperature
DallasTemperature sensors(&oneWire);
//Ця змінна потрібна буде для запису адреси датчика температури
DeviceAddress oneWireDeviceAddress; // We'll use this variable to store a found device address
int numberOfOneWireDevices;

/********* Firebase **************/
/* Define the Firebase Data object */
FirebaseData fbdo;
/* Define the FirebaseAuth data for authentication data */
FirebaseAuth auth;
/* Define the FirebaseConfig data for config data */
FirebaseConfig config;

//General configuration        
#define UPDATE_INTERVAL 60 //interval in seconds to update sensors
uint32_t updateTiming;
String startTime = "";

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  //Basic serial and led initialization
  Serial.begin(115200);

  //WiFi initialization
  Serial.println("Initializing Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  bool res = wm.autoConnect(wm.getDefaultAPName().c_str(), WM_PASSWORD); // password protected ap
  if (!res) {
    Serial.println("Failed to connect");
    //ESP.restart();
  } else {
    Serial.print("Connected with IP: ");
    Serial.println(WiFi.localIP());
  }
  
  //DS18B20 initialization
  Serial.print("Sensor device initialization...");
  // locate devices on the bus
  sensors.begin();
  numberOfOneWireDevices = sensors.getDeviceCount();
  delay(1500);
  Serial.print("Number of 1-wire devices = ");
  Serial.println(numberOfOneWireDevices, DEC);
  for (int i = 0; i < numberOfOneWireDevices; i++){
    if (sensors.getAddress(oneWireDeviceAddress, i)) {
      Serial.printf("Devices #%d = %s\n", i, convertAddressToString(oneWireDeviceAddress).c_str());
    }
  }
  Serial.println("DB18B20 ....DONE");

  //Firebase initialization
  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4096);
  // Assign the callback function for the long running token generation task 
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h
  // Assign the maximum retry of token generation 
  config.max_token_generation_retry = 5;
  // Initialize the library with the Firebase authen and config 
  Firebase.begin(&config, &auth);

  //to force immediate execution (after 3 second)
  updateTiming = millis() - UPDATE_INTERVAL * 1000 + 3 * 1000;
}

void loop() {
  // put your main code here, to run repeatedly:

  if (millis() - updateTiming < UPDATE_INTERVAL * 1000) return;
  updateTiming = millis();

  // отправить команду для получения температуры
  //requested temperature will be ready after 750 ms
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();
  //wait for 1 seconds and process buttons or other staff in this time
  uint32_t timing = millis();
  while (millis() - timing < 1000) {

  }

  //get year month day hour min
  String timePath =  getTimeAsPath(); /* yyyy/mm/dd/hh/nn */

  float temperatureC;
  for (int i = 0; i < numberOfOneWireDevices; i++){
    sensors.getAddress(oneWireDeviceAddress, i);
    String addr = convertAddressToString(oneWireDeviceAddress);
    temperatureC = sensors.getTempCByIndex(i);
    // Check if reading was successful
    if (temperatureC != DEVICE_DISCONNECTED_C) {
      Serial.println("#" + String(i) + " " + addr + " -> t" +  + " = " + String(temperatureC, 2) + ";");
      //upload to DB
      if (Firebase.ready()) {
        String fullPath = "/temperatures/" + addr + "/" + timePath + "/t";
        Serial.printf("Strore temp to t%d... %s\n", i, Firebase.setFloat(fbdo, fullPath.c_str(), temperatureC) ? "ok" : fbdo.errorReason().c_str());
        FirebaseJson json;
        json.set("t", temperatureC);
        json.set("time", timePath);
        String currentPath = "/sensors/" + addr + "/last";
        Serial.printf("Strore last temp and time to t%d... %s\n", i, Firebase.set(fbdo, currentPath.c_str(), json) ? "ok" : fbdo.errorReason().c_str());
//        String currentPath = "/sensors/" + addr + "/last/t";
//        Serial.printf("Strore last temp to t%d... %s\n", i, Firebase.setFloat(fbdo, currentPath.c_str(), temperatureC) ? "ok" : fbdo.errorReason().c_str());
//        currentPath = "/sensors/" + addr + "/last/time";
//        Serial.printf("Strore last time to t%d... %s\n", i, Firebase.setString(fbdo, currentPath.c_str(), timePath.c_str()) ? "ok" : fbdo.errorReason().c_str());

        if (startTime == ""){
          currentPath = "/sensors/" + addr + "/start/time";
          Serial.printf("Strore start time to t%d... %s\n", i, Firebase.setString(fbdo, currentPath.c_str(), timePath.c_str()) ? "ok" : fbdo.errorReason().c_str());
          currentPath = "/uptime/" + addr + "/" + timePath + "/last_time";
          Serial.printf("Insert uptime informatione to t%d... %s\n", i, Firebase.setString(fbdo, currentPath.c_str(), timePath.c_str()) ? "ok" : fbdo.errorReason().c_str());
        } else {
          currentPath = "/uptime/" + addr + "/" + startTime + "/last_time";
          Serial.printf("Update uptime informatione to t%d... %s\n", i, Firebase.setString(fbdo, currentPath.c_str(), timePath.c_str()) ? "ok" : fbdo.errorReason().c_str());
        }
     
      }
    } else {
      Serial.print("ERROR NO SENSOR!");
    }
  }
  if (startTime == "" && Firebase.ready()) startTime = timePath; //set startTime different than 0 to avoid recurent writes of sensor start time

  Serial.println();

}

String convertAddressToString(DeviceAddress deviceAddress)
{
  String addr = "";
  for (uint8_t i = 0; i < 8; i++)
  {
    if (deviceAddress[i] < 16) addr += "0";
    addr += String(deviceAddress[i], HEX);
    if (i == 0) addr += "-";
  }
  addr.toUpperCase();
  return addr;
}

String getTimeAsPath(void) {
  timeClient.update();
  
  unsigned long rawTime = timeClient.getEpochTime();
  String result = formatInt(year(rawTime), 4) + '/' + formatInt(month(rawTime), 2)  + '/' + formatInt(day(rawTime), 2) + '/' + formatInt(hour(rawTime), 2) + '/' + formatInt(minute(rawTime), 2);
  Serial.println(result);

  return result;
}

String formatInt(int n, int leadingZero) {
  String zeroes = "00000000000000000000000000000000";//32
  String result = String(n); 
  if (leadingZero < 0) leadingZero = 0;
  if (leadingZero > 32) leadingZero = 32;
  if (leadingZero > result.length()) 
    result = zeroes.substring(0, leadingZero - result.length()) + result;
  return result;
}
