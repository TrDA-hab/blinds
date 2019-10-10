#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <MQTT.h>
#include <EEPROM.h>
WiFiClient espClient;
ESP8266WebServer server(80);
MQTTClient client;

//Original code: https://github.com/andreypopov/blinds-popov
#define VERSION "7.3.1" //for NEMA-17
#define DEBUG
//==================================================================
//===SETTINGS block=================================================
//==================================================================
const char* WIFI_ssid       = "Cisco";
const char* WIFI_password   = "admin";
const char* OTA_hostname    = "Blinds-OTA-02";
const char* OTA_password    = "";
const char* MQTT_namespace  = "Blindss"; //first part of mqtt topic /[namespace]/..
const char* MQTT_device     = "blnd-2"; //second part of mqtt topic /[namespace]/[device]/
const char* MQTT_device_group = "blindss"; //second part of mqtt topic /[namespace]/[device]/
const char* MQTT_server     = "192.168.1.7";
int         MQTT_port       =  1883;
const char* MQTT_username   = "";
const char* MQTT_password   = "";
const char* MQTT_subscribe  = "command"; //for receiving /[namespace]/[device]/[subscribe]/+
bool        WEB_SERVER_ON   = true;  
const char* WEB_SERVER_username   = "1";
const char* WEB_SERVER_password   = "1";
//==================================================================
//===CUSTOM VARS====================================================
//==================================================================
//STEPPER
#include <Arduino.h>
#include "DRV8825.h" //&A4988 normal !!!

#define PIN_ENABLE D5
#define PIN_STEP D7
#define PIN_DIR D6
#define PIN_MODE0 D8 //half step mode, (optional)

int           MOTOR_STEPS       = 200;   //NEMA-17&A4988, 200 steps for one rotation
int           MOTOR_MICROSTEPS  = 1;     //1 - full step, 2 - half step
int           MOTOR_RPM         = 2000;  //NEMA-17&A4988 normal rmp is 1000-3000rpm
int           MOTOR_ACCEL       = 2500;
int           MOTOR_DECEL       = 2500;
long int      STEPPER_MAX_STEPS = 1100000;  //if use halfstep -  double value
long int      STEPPER_POSITION  = 0; 
long int      STEPPER_TARGET    = 0; 
bool          STEPPER_REVERSE   = true; //stepper direction
bool          PERCENT_REVERSE   = true; //TRUE: 100% means opened, FALSE: 100% meanes closed
int           STEPPER_STATE     = 0; //0: stop, 1: down, -1: up
unsigned long STEPPER_TIMER     = 0;
unsigned long STEPPER_TIMER_LAST    = 0;
int           SEND_WHILE_WORKING    = 0;
int           SEND_STATUS_INTERVAL  = 10; //in seconds
static long int step = 0;
 
DRV8825 stepper(MOTOR_STEPS, PIN_DIR, PIN_STEP, PIN_ENABLE); //full step
//DRV8825 stepper(MOTOR_STEPS, PIN_DIR, PIN_STEP, PIN_ENABLE, PIN_MODE0); //half step

int EEaddr__MOTOR_STEPS = 10;
int EEaddr__MOTOR_MICROSTEPS = 20;
int EEaddr__MOTOR_ACCEL = 30;
int EEaddr__MOTOR_DECEL = 40;
int EEaddr__MOTOR_RPM = 50;
int EEaddr__STEPPER_POSITION = 60;
int EEaddr__STEPPER_MAX_STEPS = 70;
int EEaddr__STEPPER_REVERSE = 80;
int EEaddr__SEND_WHILE_WORKING = 90;
int EEaddr__SEND_STATUS_INTERVAL = 100;
int EEaddr__PERCENT_REVERSE = 110;

//full step
const int STEPS_PRECISION = 7;
long stepper_calibration[] = {0, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000}; //0-100% for 9000rmp (NEMA-17&A4988)
//==================================================================

void setup() {  
  setup_custom_init();
  
  setup_serial();
  setup_wifi();
  setup_ota();
  setup_mqtt();
  setup_eeprom();
  setup_webserver();

  setup_custom();

  mqttPublish("meta/hello", "Started!");
  mqttPublish("meta/version", String(VERSION));
  mqttPublish("meta/wifi_ip", WiFi.localIP().toString());
}

void setup_serial() {
  Serial.begin(115200);
  Serial.println("Serial ready");
}

void setup_wifi() {
  delay(100);
  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(WIFI_ssid);
  WiFi.begin(WIFI_ssid, WIFI_password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
//  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
//    Serial.println("WiFi Connect Failed! Rebooting...");
//    delay(1000);
//    ESP.restart();
//  }
  randomSeed(micros());
  Serial.println("");
  Serial.println("WiFi ready");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void setup_ota() {
  //ArduinoOTA.setPort(8266); // Port defaults to 8266
  ArduinoOTA.setHostname(OTA_hostname); // Hostname defaults to esp8266-[ChipID]
  if (OTA_password) {
    ArduinoOTA.setPassword(OTA_password);
  }

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  Serial.println("OTA ready");
}

void setup_mqtt() {
  client.begin(MQTT_server, MQTT_port, espClient);
  client.clearWill();
  client.onMessage(messageReceived);
  debug("MQTT ready");
}


void loop() {
  client.loop();
  if(!client.connected()) {
    connect();
  } else {
    ArduinoOTA.handle();
    loop_webserver();
  
    loop_custom();
  }
}

void connect() {

  Serial.print("checking wifi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

  Serial.print("\nconnecting...");
  while (!client.connect(MQTT_device, MQTT_username, MQTT_password)) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println("\nconnected!");

  if (MQTT_subscribe) {
    client.subscribe("/"+String(MQTT_namespace)+"/"+String(MQTT_device)+"/"+String(MQTT_subscribe)+"/+");
    debug("MQTT subscribed to: /"+String(MQTT_namespace)+"/"+String(MQTT_device)+"/"+String(MQTT_subscribe)+"/+");

    client.subscribe("/"+String(MQTT_namespace)+"/"+String(MQTT_device_group)+"/"+String(MQTT_subscribe)+"/+");
    debug("MQTT subscribed to: /"+String(MQTT_namespace)+"/"+String(MQTT_device_group)+"/"+String(MQTT_subscribe)+"/+");
  }
}

void messageReceived(String &topic, String &payload) {
    String deviceName = getTopicPart(topic, '/', 2);
    String controlName = getTopicPart(topic, '/', 4);

    debug("IN: " + topic + ", Device: " + deviceName + ", Control: " + controlName + ", Payload: "+payload);
    exec_command(controlName, payload);
}

String getTopicPart(String data, char separator, int index) {
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void mqttPublish(String cmd, String payload) {
  String topic = "/"+String(MQTT_namespace)+"/"+String(MQTT_device)+"/"+cmd;
  client.publish(topic, payload, false, 0);
#if defined (DEBUG)
  Serial.println("MQTT OUT: "+topic+" "+payload);
#endif
}

void mqttPublishRetained(String cmd, String payload) {
  String topic = "/"+String(MQTT_namespace)+"/"+String(MQTT_device)+"/"+cmd;
  client.publish(topic, payload, true, 0);
#if defined (DEBUG)
  Serial.println("MQTT OUT (retained): "+topic+" "+payload);
#endif
}

void debug(String str) {
#if defined (DEBUG)
  Serial.println(str);
  mqttPublish("debug", str);
#endif
}
