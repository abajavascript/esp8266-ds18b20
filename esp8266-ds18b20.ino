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

//DHT sensor
#include <DHT.h>

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

/************* DHT22 *************/
#define DHTPIN 2     // Digital pin connected to the DHT sensor
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
DHT dht(DHTPIN, DHTTYPE);

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

//global variable to correctly process hot plug functionality
String startTime = "";
String prevProcessedSensorsList = ":";
int processedSensorsCnt = 0;
String processedSensorsList = ":";

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  //Basic serial and led initialization
  Serial.begin(115200);

  //WiFi initialization
  Serial.println("Initializing Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);

  bool res = wm.autoConnect(wm.getDefaultAPName().c_str(), WM_PASSWORD); // password protected ap
  if (!res) {
    Serial.println("Failed to connect");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  } else {
    Serial.print("Connected with IP: ");
    Serial.println(WiFi.localIP());
  }
  
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

  //wait for some period of second to collect and upload sensors only onece per UPDATE_INTERVAL seconds
  if (millis() - updateTiming < UPDATE_INTERVAL * 1000) return;
  updateTiming = millis();

  //Check NTP time
  //get year month day hour min
  String timePath =  getTimeAsPath(); /* yyyy/mm/dd/hh/nn or empty is error*/
  if (!timePath.length()) return;

  //to chack hot plug of sensors and make necessary updates in case sensor configuration changed
  processedSensorsCnt = 0;
  processedSensorsList = ":";

  //Search for DS18B20 sensors
  Serial.printf("Search for DS18B20 Sensors ...");
  sensors.begin();
  int numberOfOneWireDevices = sensors.getDeviceCount();
  Serial.printf("Found %d of 1-wire devices.\n", numberOfOneWireDevices);

  //We have connected some one wire sensors
  if (numberOfOneWireDevices > 0){
    // отправить команду для получения температуры
    //requested temperature will be ready after 750 ms
    sensors.setWaitForConversion(false);
    sensors.requestTemperatures();
    //wait for 1 seconds and process buttons or other staff in this time
    uint32_t timing = millis();
    while (millis() - timing < 1000) {

    }

    //process all DS18B20 sensors in a loop
    float temperatureC;
    for (int i = 0; i < numberOfOneWireDevices; i++){
      sensors.getAddress(oneWireDeviceAddress, i);
      String addr = convertAddressToString(oneWireDeviceAddress);
      Serial.printf("Processing Devices #%d = %s", i, addr.c_str());

      temperatureC = sensors.getTempC(oneWireDeviceAddress);
      // Check if reading was successful
      if (temperatureC != DEVICE_DISCONNECTED_C) {
        Serial.println(" -> t = " + String(temperatureC, 2) + ";");
        fbStoreSensorData(addr, timePath, temperatureC, 0, 0);
      } else {
        Serial.println("ERROR NO SENSOR!");
      }
    }
  } else {
    //no 1-wire devices, search fo DHT
    Serial.printf("Search for DHT Sensors ...");
    dht.begin();
    delay(2000);

    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
    float h = dht.readHumidity();
    // Read temperature as Celsius (the default)
    float t = dht.readTemperature();
    // Check if any reads failed and exit early (to try again).
    if (!(isnan(h) || isnan(t))) {//successfully read values
      // Compute heat index in Celsius (isFahreheit = false)
      float hic = dht.computeHeatIndex(t, h, false);
      String addr = WiFi.macAddress();//use ESP unique MAC as sensor id
      addr.replace(":", "-");
      Serial.printf("Processing DHT Devices = %s", addr.c_str());
      Serial.println(" -> t = " + String(t, 2) + "; h = " + String(h, 2) + "; hic = " + String(hic, 2) + ";");
      fbStoreSensorData(addr, timePath, t, h, hic);
    } else {
      Serial.println("ERROR NO SENSOR!");
    }
  }

  if (processedSensorsList != ":") prevProcessedSensorsList = processedSensorsList;
  //only if start time was written
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
  if (year(rawTime) < 2020) return String(); //something wrong with time servers
  String result = formatInt(year(rawTime), 4) + '/' + formatInt(month(rawTime), 2)  + '/' + formatInt(day(rawTime), 2) + '/' + formatInt(hour(rawTime), 2) + '/' + formatInt(minute(rawTime), 2);
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


bool fbStoreSensorData(String addr, String timePath, float temperatureC, float humidity, float heatIndex){
  //upload to DB
  if (!Firebase.ready()) return false;
  String currentPath;
  
  FirebaseJson jsonT;
  jsonT.set("t", temperatureC);
  if (humidity > 0) {
    jsonT.set("h", humidity);
    jsonT.set("ti", heatIndex);
  }
  currentPath = "/temperatures/" + addr + "/" + timePath;
  Serial.printf("Strore temp and hum to %s... %s\n", currentPath.c_str(), Firebase.set(fbdo, currentPath.c_str(), jsonT) ? "ok" : fbdo.errorReason().c_str());

  FirebaseJson jsonL;
  jsonL.set("t", temperatureC);
  jsonL.set("time", timePath);
  currentPath = "/sensors/" + addr + "/last";
  Serial.printf("Strore last temp and time to %s... %s\n", currentPath.c_str(), Firebase.set(fbdo, currentPath.c_str(), jsonL) ? "ok" : fbdo.errorReason().c_str());

  processedSensorsCnt++;
  processedSensorsList+=addr+':';

  if (prevProcessedSensorsList.indexOf(":"+addr+":") < 0) {
    currentPath = "/sensors/" + addr + "/start/time";
    Serial.printf("Strore start time to %s... %s\n", currentPath.c_str(), Firebase.setString(fbdo, currentPath.c_str(), timePath.c_str()) ? "ok" : fbdo.errorReason().c_str());
    currentPath = "/sensorsList/" + addr;
    Serial.printf("Strore sensor id to %s... %s\n", currentPath.c_str(), Firebase.setString(fbdo, currentPath.c_str(), timePath.c_str()) ? "ok" : fbdo.errorReason().c_str());
    currentPath = "/uptime/" + addr + "/" + timePath + "/last_time";
    Serial.printf("Insert uptime informatione to %s... %s\n", currentPath.c_str(), Firebase.setString(fbdo, currentPath.c_str(), timePath.c_str()) ? "ok" : fbdo.errorReason().c_str());
    startTime = timePath;//update start time when new sensors added
  } else {
    if (startTime != ""){
      currentPath = "/uptime/" + addr + "/" + startTime + "/last_time";
      Serial.printf("Update uptime informatione to %s... %s\n", currentPath.c_str(), Firebase.setString(fbdo, currentPath.c_str(), timePath.c_str()) ? "ok" : fbdo.errorReason().c_str());
    }
  }
  return true;
}
