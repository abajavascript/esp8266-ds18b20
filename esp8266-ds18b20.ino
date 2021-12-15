//General ESP8266 WiFi staff
#include <ESP8266WiFi.h>

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

/*********  WiFi credentials **************/
#define WIFI_SSID "SSID"
#define WIFI_PASSWORD "PASS"

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

/********* Firebird **************/
#define API_KEY "AIzaSyBFmiUTEUt64h0eoLpYeqaIJjELbbE-UrE"
/* Define the user Email and password that already registerd or added in your project */
#define USER_EMAIL "user@email.com"
#define USER_PASSWORD "password"
#define DATABASE_URL "https://sample-storage-a309d.firebaseio.com" //<databaseName>.firebaseio.com or <databaseName>.<region>.firebasedatabase.app
/* Define the Firebase Data object */
FirebaseData fbdo;
/* Define the FirebaseAuth data for authentication data */
FirebaseAuth auth;
/* Define the FirebaseConfig data for config data */
FirebaseConfig config;
//            authDomain: "sample-storage-a309d.firebaseapp.com",
//            projectId: "sample-storage-a309d",
//            storageBucket: "sample-storage-a309d.appspot.com",
//            messagingSenderId: "494050894036",
//            appId: "1:494050894036:web:c6b39b015367284925db41"
        
#define UPDATE_INTERVAL 60 //interval in seconds to update sensors
uint32_t updateTiming;
uint32_t startTime= 0;

void setup() {
  // put your setup code here, to run once:
  pinMode(LED_BUILTIN, OUTPUT);
  //Basic serial and led initialization
  Serial.begin(115200);

  //WiFi initialization
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

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
      Serial.print("Devices #");
      Serial.print(i, DEC);
      Serial.print(" = ");
      Serial.print(convertAddressToString(oneWireDeviceAddress));
      Serial.println();
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

  updateTiming = millis() - UPDATE_INTERVAL * 1000;
}

void loop() {
  // put your main code here, to run repeatedly:

  if (millis() - updateTiming < UPDATE_INTERVAL * 1000) return;
  updateTiming = millis();

  //get year month day hour
  String path =  getTimeAsPath();

  // отправить команду для получения температуры
  //requested temperature will be ready after 750 ms
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();
  //wait for 1 seconds and process buttons or other staff in this time
  uint32_t timing = millis();
  while (millis() - timing < 1000) {

  }
  //in async mode temperature will be ready after 750 ms
  float temperatureC;
  for (int i = 0; i < numberOfOneWireDevices; i++){
    sensors.getAddress(oneWireDeviceAddress, i);
    String addr = convertAddressToString(oneWireDeviceAddress);
    temperatureC = sensors.getTempCByIndex(i);
    // Check if reading was successful
    if (temperatureC != DEVICE_DISCONNECTED_C) {
      Serial.print(addr + " -> t");
      Serial.print(i);
      Serial.print(" = ");
      Serial.print(temperatureC);
      Serial.print("; ");
      //upload to DB
      if (Firebase.ready()) {
        //path += auth.token.uid.c_str(); //<- user uid of current user that sign in with Emal/Password
        String fullPath = "/temperatures/" + addr + "/" + path + "/t";
        Serial.printf("Strore temp to t%d... %s\n", i, Firebase.setFloat(fbdo, fullPath.c_str(), temperatureC) ? "ok" : fbdo.errorReason().c_str());
        String currentPath = "/sensors/" + addr + "/last/t";
        Serial.printf("Strore last temp to t%d... %s\n", i, Firebase.setFloat(fbdo, currentPath.c_str(), temperatureC) ? "ok" : fbdo.errorReason().c_str());
        currentPath = "/sensors/" + addr + "/last/time";
        Serial.printf("Strore last time to t%d... %s\n", i, Firebase.setString(fbdo, currentPath.c_str(), path.c_str()) ? "ok" : fbdo.errorReason().c_str());
        if (!startTime){
          currentPath = "/sensors/" + addr + "/start/t";
          Serial.printf("Strore start time to t%d... %s\n", i, Firebase.setString(fbdo, currentPath.c_str(), path.c_str()) ? "ok" : fbdo.errorReason().c_str());
        }
      }
    } else {
      Serial.print("ERROR NO SENSOR!");
    }
  }
  startTime = timeClient.getEpochTime(); //set startTime different than 0 to avoid recurent writes

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
