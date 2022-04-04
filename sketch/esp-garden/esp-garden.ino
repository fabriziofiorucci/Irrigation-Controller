/*
 * esp-garden
 * 
 * RTClib by Adafruit
 * PCF8574 library by Renzo Mischianti - https://www.mischianti.org/2019/01/02/pcf8574_1-i2c-digital-i-o-expander-fast-easy-usage/
 * ArduinoJSON by Benoît Blanchon - https://arduinojson.org/
 * 
 * D1 = i2c SCL
 * D2 = i2c SDA
 * D3 = Water flow sensor input
 * 
 * i2c:
 * DS1307 - 0x50, 0x68
 * PCF8574 - 0x20
 * 
 * EEPROM locations content (per water line "x")
 * x     = start hour
 * x + 1 = start minute
 * x + 2 = duration
 * x + 3 = enabled (1) or disabled (1)
 * 
 * MQTT commands to topic esp-garden/control
 * 
 */

#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include "RTClib.h"
#include "PCF8574.h"

// Custom parameters - TO BE EDITED
const char* ssid = "WIFI_SSID";
const char* password = "WIFI_PASSWORD";

const char* mqttServer = "MQTT_BROKER_IP_OR_HOSTNAME";
const char* mqttClientId="esp-garden";
const char* mqttClientPassword="MQTT_CLIENT_PASSWORD";

const char* otaPassword = "OTA_PASSWORD";

const char* ntpServer = "it.pool.ntp.org";

const char* controlTopic = "esp-garden/control";
const char* debugTopic = "esp-garden/debug";
const char* gardenTopicSet = "esp-garden/water/set";
const char* gardenTopicState = "esp-garden/water/state/";
const char* gardenTopicWaterFlow = "esp-garden/water/usage";

da
// DON'T CHANGE ANYTHING HERE BELOW

const int   WATER_LINES = 4;

// NTP and RTC
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, 3600, 60000);
RTC_DS1307 rtc;

// WiFi
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// GPIO expander
PCF8574 pcf8574(0x20);

// Water flow sensor
int WATER_FLOW_PIN=D3;
long waterFlowPulseCount=0;
unsigned long msIRQdebouncingTime = 1;
volatile unsigned long lastIRQmicros;

// Timed MQTT publish
long lastMsg = 0;
long pollInterval=2000;

// Current configuration
struct lineConfig {
  boolean enabled;
  int startHour;
  int startMinute;
  int duration;
  boolean running;
  uint32_t switchOffUTC;
};

struct lineConfig linesConfig[WATER_LINES];
boolean fetchConfigFromEEPROM = true;


void setup_wifi() {
  delay(10);

  pinMode(LED_BUILTIN, OUTPUT);

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  //WiFi.config(ip, gateway, subnet);
  WiFi.begin(ssid, password);

  for (int i = 0; i < 500 && WiFi.status() != WL_CONNECTED; i++) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(25);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(25);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Restarting ESP due to loss of Wi-Fi");
    ESP.restart();
  }

  randomSeed(micros());

  Serial.println("");
  Serial.print("WiFi connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnectMqtt() {
  while (!mqttClient.connected()) {
    String clientId = mqttClientId;

    Serial.print("Attempting MQTT connection...");
    
    if (mqttClient.connect(clientId.c_str(),mqttClientId,mqttClientPassword)) {
      Serial.println("connected to MQTT");
      dumpDebug("connected to MQTT");
      mqttClient.subscribe(gardenTopicSet);
      mqttClient.subscribe(controlTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 3 seconds");
      delay(3000);
    }
  }
}

/**
 * Builds a serialized JSON debug message
 */
String mkDebugMessage(String status, String description) {
  StaticJsonDocument<1024> msg;

  msg["status"] = status;
  msg["description"] = description;

  String debugString;
  serializeJson(msg, debugString);

  return debugString;
}

/*
 * Dumps debug to Serial port and MQTT topic
 */
void dumpDebug(String s) {
  Serial.println(s);
  mqttClient.publish(debugTopic,(char*)(s).c_str());
}

/**
 * Returns current configuration JSON
 */
String dumpConfig() {
  StaticJsonDocument<1024> jsonConfig;

  Serial.print("DUMPCONFIG: fetch from EEPROM ");
  Serial.println(fetchConfigFromEEPROM);
  
  JsonObject rtcTime = jsonConfig.createNestedObject("rtcTime");
  JsonObject ntpTime = jsonConfig.createNestedObject("ntpTime");
  JsonObject networkConfig = jsonConfig.createNestedObject("networkConfig");
  JsonArray linesConfigJson = jsonConfig.createNestedArray("linesConfig");

  // Hostname
  jsonConfig["host"] = mqttClientId;
  
  // RTC time
  DateTime now = rtc.now();
  rtcTime["hour"] = now.hour();
  rtcTime["minute"] = now.minute();
  rtcTime["second"] = now.second();
  rtcTime["utc"] = now.unixtime();

  // NTP time
  ntpTime["hour"] = timeClient.getHours();
  ntpTime["minute"] = timeClient.getMinutes();
  ntpTime["second"] = timeClient.getSeconds();

  // Network configuration
  networkConfig["ipAddress"] = WiFi.localIP();

  // Get water lines configuration
  for (int i=0; i<WATER_LINES; i++) {
    StaticJsonDocument<512> thisLineConfig;

    Serial.print("LINE ");
    Serial.print(i);
    Serial.print(" ");
    Serial.println(linesConfig[i].running);

    // Fetch configuration stored in EEPROM
    if (fetchConfigFromEEPROM == true) {
      linesConfig[i].startHour = EEPROM.read(i*4);
      linesConfig[i].startMinute = EEPROM.read(i*4+1);
      linesConfig[i].duration = EEPROM.read(i*4+2);
      linesConfig[i].enabled = EEPROM.read(i*4+3);
      linesConfig[i].running = false;
      linesConfig[i].switchOffUTC = 0;
    }

    String sEnabled = "false";
    if (linesConfig[i].enabled == 1) {
      sEnabled = "true";
    }

    String sRunning = "false";
    if (linesConfig[i].running == true) {
      sRunning="true";
    }

    JsonObject startJson = thisLineConfig.createNestedObject("start");
    JsonObject runningJson = thisLineConfig.createNestedObject("running");

    thisLineConfig["line"] = i;
    thisLineConfig["enabled"] = sEnabled;
    
    startJson["hour"] = linesConfig[i].startHour;
    startJson["minute"] = linesConfig[i].startMinute;
    startJson["duration"] = linesConfig[i].duration;

    runningJson["status"] = sRunning;
    runningJson["offUTCTime"] = linesConfig[i].switchOffUTC;

    linesConfigJson.add(thisLineConfig);
  }

  fetchConfigFromEEPROM = false;

  String configString;
  serializeJson(jsonConfig, configString);
  
  dumpDebug(configString);

  return configString;
}

/**
 * Sets the water line status
 * 
 * waterLine = 0-<WATER_LINES-1>
 * waterStatus = 0|1
 */
void lineToggle(int waterLine,int waterStatus) {
  int relayToToggle=waterLine*2+waterStatus;

  dumpDebug(mkDebugMessage("done","Line "+String(waterLine)+" set to "+String(waterStatus)+" relay "+relayToToggle));

  pcf8574.digitalWrite(waterLine*2+waterStatus,0);
  delay(200);
  pcf8574.digitalWrite(waterLine*2+waterStatus,1);

  if (waterStatus == 0) {
    Serial.print("RUNNING TO FALSE line ");
    Serial.println(waterLine);
    linesConfig[waterLine].running = false;
    linesConfig[waterLine].switchOffUTC = 0;
  } else {
    Serial.print("RUNNING TO TRUE line ");
    Serial.println(waterLine);
    linesConfig[waterLine].running = true;
  }

  String line=gardenTopicState+String(waterLine);
  mqttClient.publish((char*)line.c_str(), (char*)String(waterStatus).c_str());
}

/*
 * MQTT commands in JSON format
 */
void callback(char* topic, byte* payload, unsigned int length) {
  // Deserialize JSON MQTT payload
  DynamicJsonDocument jsonCommand(2048);
  deserializeJson(jsonCommand, payload);

  String command=String(jsonCommand["command"]);

  if (command == "dump") {
    dumpConfig();
  } else if (command == "restart") {
    dumpDebug(mkDebugMessage("done","Restarting"));
    delay(1000);
    ESP.restart();
  } else if (command == "configure") {
    boolean configChanged = false;
    boolean isError = false;
    JsonArray lines = jsonCommand["linesConfig"].as<JsonArray>();

    for (JsonVariant thisLine : lines) {
      int waterLine = thisLine["line"].as<int>();
      boolean state = thisLine["enabled"].as<boolean>();

      boolean isStartAvailable = thisLine.containsKey("start");
      int startHour = thisLine["start"]["hour"].as<int>();
      int startMinute = thisLine["start"]["minute"].as<int>();
      int duration = thisLine["start"]["duration"].as<int>();
      
      if (waterLine >= 0 and waterLine < WATER_LINES) {
          if (startHour < 24) {
            if (startMinute < 60) {
              if (duration < 60) {
                if (state == 0 || state == 1) {
                  /*
                    * EEPROM locations content (per water line "x")
                    * x     = start hour
                    * x + 1 = start minute
                    * x + 2 = duration
                    * x + 3 = enabled (1) or disabled (1)
                    */

                  if (isStartAvailable == true) {
                    EEPROM.write(waterLine*4,startHour);
                    EEPROM.write(waterLine*4+1,startMinute);
                    EEPROM.write(waterLine*4+2,duration);
                  }
                  EEPROM.write(waterLine*4+3,state);
                  configChanged = true;

                  if (state == 0) {
                    lineToggle(waterLine,0);
                  }
                }
                else {
                  dumpDebug(mkDebugMessage("error","Invalid state " + String(state)));
                  isError = true;
                }
              } else {
                dumpDebug(mkDebugMessage("error","Invalid duration " + String(duration)));
                isError = true;
              }
            } else {
              dumpDebug(mkDebugMessage("error","Invalid minute " + String(startMinute)));
              isError = true;
            }
          } else {
            dumpDebug(mkDebugMessage("error","Invalid hour " + String(startHour)));
            isError = true;
          }
        } else {
          dumpDebug(mkDebugMessage("error","Invalid line number " + String(waterLine)));
          isError = true;
        }
      }

      if (configChanged == true) {
        fetchConfigFromEEPROM = true;
        EEPROM.commit();
      }
    } else if (command == "set") {
      JsonArray lines = jsonCommand["linesConfig"].as<JsonArray>();

      for (JsonVariant thisLine : lines) {
        int waterLine = thisLine["line"].as<int>();
        boolean state = thisLine["running"].as<boolean>();

        if (waterLine >= 0 and waterLine < WATER_LINES) {
          if (state == true) {
            lineToggle(waterLine,1);
          } else {
            lineToggle(waterLine,0);
          }
        } else {
          dumpDebug(mkDebugMessage("error","Invalid line number " + String(waterLine)));
        }
      }
    }
}

void ICACHE_RAM_ATTR onWaterFlowPulse()
{
  if((long)(micros() - lastIRQmicros) >= msIRQdebouncingTime * 1000) {
    waterFlowPulseCount++; 
    lastIRQmicros = micros();
  } 
}

void setup() {
  // EEPROM init
  EEPROM.begin(512);
  
  Serial.begin(115200, SERIAL_8N1);
  Serial.println("-----------------------------------------");
  Serial.printf("Chip ID     : %08X\n", ESP.getChipId());
  Serial.printf("Core version: %s\n", ESP.getCoreVersion().c_str());
  Serial.printf("SDK version : %s\n", ESP.getSdkVersion());
  Serial.printf("CPU speed   : %dMhz\n", ESP.getCpuFreqMHz());
  Serial.printf("Free Heap   : %d\n", ESP.getFreeHeap());
  Serial.printf("Reset reason: %s\n", ESP.getResetReason().c_str());
  Serial.printf("EEPROM size : %d\n", EEPROM.length());
  Serial.println("-----------------------------------------");

  pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output

  // WiFi init
  setup_wifi();

  // NTP init
  timeClient.begin();

  // Water flow sensor init
  attachInterrupt(digitalPinToInterrupt(WATER_FLOW_PIN), onWaterFlowPulse, FALLING);

  // MQTT init
  mqttClient.setServer(mqttServer, 1883);
  mqttClient.setCallback(callback);
  mqttClient.setBufferSize(1024);
  reconnectMqtt();

  // DS1307 init
  if (! rtc.begin()) {
    Serial.println("DS1307 RTC offline");
    dumpDebug("DS1307 RTC offline");
    Serial.flush();
  }

  if (! rtc.isrunning()) {
    //Serial.println("DS1307 RTC - Setting time from NTP");
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    //rtc.adjust(DateTime(2021, 12, 18, timeClient.getHours(), timeClient.getMinutes(), timeClient.getSeconds()));
  }

  // PCF8574 init
  Serial.print("Initializing PCF8574: ");

  pcf8574.pinMode(P0, OUTPUT, HIGH);
  pcf8574.pinMode(P1, OUTPUT, HIGH);
  pcf8574.pinMode(P2, OUTPUT, HIGH);
  pcf8574.pinMode(P3, OUTPUT, HIGH);
  pcf8574.pinMode(P4, OUTPUT, HIGH);
  pcf8574.pinMode(P5, OUTPUT, HIGH);
  pcf8574.pinMode(P6, OUTPUT, HIGH);
  pcf8574.pinMode(P7, OUTPUT, HIGH);

  if (pcf8574_1.begin()){
    Serial.println("PCF8574 ok");
  }else{
    Serial.println("PCF8574 failed");
  }

  // Fetches stored configuration and initializes linesConfig[]
  dumpConfig();

  // Turns all lines off
  for (int i=0;i<WATER_LINES;i++) {
    lineToggle(i,0);
  }

  // OTA init
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(mqttClientId);
  ArduinoOTA.setPassword(otaPassword);

  ArduinoOTA.onStart([]() {
    dumpDebug(mkDebugMessage("done","OTA Start"));
  });
  ArduinoOTA.onEnd([]() {
    dumpDebug(mkDebugMessage("done","OTA End"));
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    dumpDebug(mkDebugMessage("done","OTA in progress "+String(progress / (total / 100))));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) dumpDebug(mkDebugMessage("done","OTA Auth failed"));
    else if (error == OTA_BEGIN_ERROR) dumpDebug(mkDebugMessage("done","OTA Begin failed"));
    else if (error == OTA_CONNECT_ERROR) dumpDebug(mkDebugMessage("done","OTA Connect failed"));
    else if (error == OTA_RECEIVE_ERROR) dumpDebug(mkDebugMessage("done","OTA Receive failed"));
    else if (error == OTA_END_ERROR) dumpDebug(mkDebugMessage("done","OTA End failed"));
  });
  ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();
  timeClient.update();

  if (!mqttClient.connected()) {
    reconnectMqtt();
  }

  mqttClient.loop();

  long now = millis();
  if (now - lastMsg > pollInterval) {
    lastMsg = now;

    dumpDebug(mkDebugMessage("done","NTP "+timeClient.getFormattedTime()+" "+timeClient.getHours()+":"+timeClient.getMinutes()+":"+timeClient.getSeconds()));

    DateTime now = rtc.now();

    int currentHour = now.hour();
    int currentMins = now.minute();
    int currentSecs = now.second();
    uint32_t currentUTC = now.unixtime();

    dumpDebug(mkDebugMessage("done","RTC "+String(currentHour)+":"+String(currentMins)+":"+String(currentSecs)+" UTC "+String(currentUTC)));

    for (int i=0;i<WATER_LINES;i++) {

      if (linesConfig[i].enabled == true) {
        if (currentHour == linesConfig[i].startHour && currentMins == linesConfig[i].startMinute && linesConfig[i].running == false) {
          // Turns on water line
          lineToggle(i,1);

          // Sets the UTC time to switch line off
          linesConfig[i].switchOffUTC=now.unixtime()+60*linesConfig[i].duration;
        }

        if (linesConfig[i].switchOffUTC > 0 && now.unixtime() >= linesConfig[i].switchOffUTC && linesConfig[i].running == true) {
          // Turns off water line - switchOffUTC is set to 0 if line was manually enabled
          lineToggle(i,0);
          linesConfig[i].switchOffUTC = 0;
        }
      }
      
      //dumpDebug("L"+String(i)+" enabled["+String(linesConfig[i].enabled)+"] start["+
      //  String(linesConfig[i].startHour)+":"+String(linesConfig[i].startMinute)+"] duration["+
      //  String(linesConfig[i].duration)+"]");
    }

    // Rain tipping bucket
    long waterFlowReading=waterFlowPulseCount;
    waterFlowPulseCount=0;

    mqttClient.publish(gardenTopicWaterFlow, (char*)String(waterFlowReading).c_str());
  }
}
