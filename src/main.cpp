#include <Arduino.h>
#include "../../renegade_members.h"
#include <Preferences.h>
#include <espnow.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Spi.h>
#include <Adafruit_ADS1X15.h>

#define NAME_NODE "Bat_8266"

Adafruit_ADS1115 ads;

struct struct_sensor_reading {
    const char* Name;
    int         Type;      //1=Amp, 2=Volt
    int         IOPort;
    float       NullWert;
    float       VperAmp;
    float       Vin;
    float       Value;
};
struct struct_registered_monitor {
    char       MonitorName[20] = {};
    u_int8_t   BroadcastAddress[6];
    uint32_t   TimestampLastSeen = 0;
    int        Type = 0;
};

struct_sensor_reading SensorReading[ANZ_SENSOR];
struct_registered_monitor M[MAX_MONITOR];
u_int8_t TempBroadcast[6];
bool ReadyToPair = false;

int AnzMonitor = 0;

String jsondata;
StaticJsonDocument<300> doc;

uint32_t TimestampLastSend    = 0;
uint32_t TimestampLastContact = 0;
uint32_t TimestampSend = 0;
uint32_t TimestampPair = 0;


bool   Debug     = true;
bool   SleepMode = true;

Preferences preferences;

void   SendMessage();
int    SendPairingReuest();
void   Eichen();
void   OnDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len);
void   OnDataSent(uint8_t *mac_addr, uint8_t sendStatus);
void   SaveMonitors();
void   GetMonitors();
void   ClearMonitors();
void   printMAC(const uint8_t * mac_addr);


void SaveMonitors() {
  preferences.begin("Jeepify", false);
  char Buf[10] = {};
  char BufNr[5] = {};
  char BufB[5] = {};
  String BufS;

  AnzMonitor = 0;
  for (int zNr=0; zNr< MAX_MONITOR; zNr++) {
    if (M[zNr].Type > 0) {
      AnzMonitor++;
      sprintf(BufNr, "%d", zNr); strcpy(Buf, "Type-"); strcat(Buf, BufNr);
      Serial.print("schreibe "); Serial.print(Buf); Serial.print(" = "); Serial.println(M[zNr].Type);
      if (preferences.getInt(Buf, 0) != M[zNr].Type) preferences.putInt(Buf, M[zNr].Type);
      
      strcpy(Buf, "Name-"); strcat(Buf, BufNr);
      BufS = M[zNr].MonitorName;
      Serial.print("schreibe "); Serial.print(Buf); Serial.print(" = "); Serial.println(BufS);
      if (preferences.getString(Buf, "") != BufS) preferences.putString(Buf, BufS);
      
      for (int b=0; b<6; b++) {
        sprintf(BufB, "%d", b); 
        strcpy(Buf, "B"); strcat(Buf, BufB); strcat (Buf, "-"); strcat (Buf, BufNr);
        if (preferences.getUChar(Buf, 0) != M[zNr].BroadcastAddress[b]) preferences.putUChar(Buf, M[zNr].BroadcastAddress[b]);
      }
    }
  }
  if (preferences.getInt("AnzMonitor") != AnzMonitor) preferences.putInt("AnzMonitor", AnzMonitor);

  preferences.end();
}
void GetMonitors() {
  preferences.begin("Jeepify", true);
  
  char Buf[10] = {};
  char BufNr[5] = {};
  char BufB[5] = {};
  String BufS;
  
  AnzMonitor = 0;
  for (int zNr=0; zNr< MAX_MONITOR; zNr++) {
    // "Type-0"
    sprintf(BufNr, "%d", zNr); strcpy(Buf, "Type-"); strcat(Buf, BufNr);
    if (preferences.getInt(Buf) > 0) {
      AnzMonitor++;
      M[zNr].Type = preferences.getInt(Buf);
      for (int b=0; b<6; b++) {
        sprintf(BufB, "%d", b); 
        strcpy(Buf, "B"); strcat(Buf, BufB); strcat (Buf, "-"); strcat (Buf, BufNr);
        M[zNr].BroadcastAddress[b] = preferences.getUChar(Buf);
      }
      M[zNr].TimestampLastSeen = millis();
      strcpy(Buf, "Name-"); strcat(Buf, BufNr);
      BufS = preferences.getString(Buf);
      strcpy(M[zNr].MonitorName, BufS.c_str());
    }
  }
  preferences.end();
}
void ReportMonitors() {
  for (int i=0; i<MAX_MONITOR; i++) {
    Serial.println(M[i].MonitorName);
    Serial.println(M[i].Type);
    Serial.print("MAC: "); printMAC(M[i].BroadcastAddress);
    Serial.println();
    Serial.println();
  }
}
void RegisterMonitors() {
  if (esp_now_add_peer(broadcastAddressAll, ESP_NOW_ROLE_COMBO, 0, NULL, 0) != 0) { 
      Serial.println("Failed to add peer"); 
  }
  else {
    Serial.println("Peer. Broadcast added...");
  }

  for (int zNr=0; zNr<MAX_MONITOR; zNr++) {
    if (M[zNr].Type > 0) {
      if (esp_now_add_peer(M[zNr].BroadcastAddress, ESP_NOW_ROLE_COMBO, 0, NULL, 0) != 0) { 
        Serial.println("Failed to add peer"); 
      }
      else {
      Serial.print("Peer: "); Serial.print(M[zNr].MonitorName); 
      Serial.print (" ("); printMAC(M[zNr].BroadcastAddress); Serial.println(") added...");
      }
    }
  }
}
void ClearMonitors() {
  preferences.begin("Jeepify", false);
    preferences.clear();
    Serial.println("Jeepify cleared...");
  preferences.end();
  
}
int  SendPairingReuest () {
  jsondata = "";  //clearing String after data is being sent
  doc.clear();
  
  doc["Node"]    = NAME_NODE;   
  doc["Pairing"] = "add me";
  doc["Type"]    = BATTERY_SENSOR;
  doc["S0"]      = SensorReading[0].Name;
  doc["S1"]      = SensorReading[1].Name;
  doc["S2"]      = SensorReading[2].Name;
  doc["S3"]      = SensorReading[3].Name;
  doc["S4"]      = SensorReading[4].Name;

  serializeJson(doc, jsondata);  

  esp_now_send(broadcastAddressAll, (uint8_t *) jsondata.c_str(), 200);  //Sending "jsondata"  
  
  Serial.println(jsondata);   
  jsondata = "";  //clearing String after data is being sent  

  return 1;                                         
}
void SendMessage () {
  char buf[10];
  doc.clear();
  jsondata = "";

  doc["Node"] = NAME_NODE;   

  for (int s = 0; s < ANZ_SENSOR-1 ; s++) {
    Serial.println(SensorReading[s].Name);
    float TempVal = ads.readADC_SingleEnded(s);
    if (Debug) { Serial.print("TempVal:  "); Serial.println(TempVal,4); }
    float TempVolt = ads.computeVolts(TempVal);
    if (Debug) {
      Serial.print("TempVolt: "); Serial.println(TempVolt,4);
      Serial.print("Nullwert: "); Serial.println(SensorReading[s].NullWert,4);
      Serial.print("VperAmp:  "); Serial.println(SensorReading[s].VperAmp,4);
    }
    float TempAmp = (TempVolt - SensorReading[s].NullWert) / SensorReading[s].VperAmp;
    if (Debug) { Serial.print("TempAmp:   "); Serial.println(TempAmp,4); }

    if (abs(TempAmp) < 0.1) TempAmp = 0;
    SensorReading[s].Value = TempAmp;

  }
  
  SensorReading[4].Value = analogRead(SensorReading[4].IOPort) / SensorReading[4].Vin;
  
  for (int s = 0; s < ANZ_SENSOR ; s++) {
    dtostrf(SensorReading[s].Value, 0, 2, buf);
    doc[SensorReading[s].Name] = buf;
  }

  serializeJson(doc, jsondata);  
  
  for (int zNr=0; zNr<MAX_MONITOR; zNr++) {
    if (M[zNr].Type > 0) {
      Serial.print("Sending to: "); Serial.println(M[zNr].MonitorName); 
      Serial.print(" ("); printMAC(M[zNr].BroadcastAddress); Serial.println(")");
      esp_now_send(M[zNr].BroadcastAddress, (uint8_t *) jsondata.c_str(), 200);  //Sending "jsondata"  
      Serial.println(jsondata);
      Serial.println();
    }
  }

  Serial.print("\nSending: ");
  Serial.println(jsondata);
}
void Eichen() {
  Serial.println("Eichen...");
  preferences.begin("Jeepify", false);

  if (SensorReading[0].NullWert > 6) {
      SensorReading[0].NullWert = 2.5;
      SensorReading[1].NullWert = 2.5;
      SensorReading[2].NullWert = 2.5;
      SensorReading[3].NullWert = 2.5;
  }
  else {
    for(int s = 0; s < ANZ_SENSOR-1; s++) {
      float TempVal = ads.readADC_SingleEnded(s);
      Serial.println(TempVal);
      float TempVolt = ads.computeVolts(TempVal);
      Serial.println(TempVolt);
      SensorReading[s].NullWert = TempVolt;
    }
  }

  preferences.putFloat("Nullwert0", SensorReading[0].NullWert);
  preferences.putFloat("Nullwert1", SensorReading[1].NullWert);
  preferences.putFloat("Nullwert2", SensorReading[2].NullWert);
  preferences.putFloat("Nullwert3", SensorReading[3].NullWert);

  if (Debug) {
    Serial.print("get(Null[0]: ");
    Serial.print(preferences.getFloat("Nullwert0"),4);
    Serial.print("- get(Null[1]: ");
    Serial.print(preferences.getFloat("Nullwert1"),4);
    Serial.print("- get(Null[2]: ");
    Serial.print(preferences.getFloat("Nullwert2"),4);
    Serial.print("- get(Null[3]: ");
    Serial.print(preferences.getFloat("Nullwert3"),4);
    Serial.print("\n");
  }

  preferences.end();
}

void OnDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  char* buff = (char*) incomingData;        //char buffer
  jsondata = String(buff);                  //converting into STRING
  Serial.print("Recieved ");
  Serial.println(jsondata);    //Complete JSON data will be printed here
  
  DeserializationError error = deserializeJson(doc, jsondata);
  jsondata = "";  //clearing String 
  
  if (!error) {
    float TempSens;
    if (doc["Order"] == "stay alive") {
      TimestampLastContact = millis();
      if (Debug) { Serial.print("LastContact: "); Serial.println(TimestampLastContact); }
    }
    if (doc["Order"] == "Eichen") Eichen();
    if (doc["Order"] == "BatSleepMode On") {
      preferences.begin("Jeepify", false);
      preferences.putBool("SleepMode", true);
      preferences.end();
    }
    if (doc["Order"] == "BatSleepMode Off") {
      preferences.begin("Jeepify", false);
      preferences.putBool("SleepMode", false);
      preferences.end();
    }
    if (doc["Order"] == "VoltCalib") {
      int TempRead = analogRead(SensorReading[4].IOPort);
        
      Serial.print("Volt(vorher) = ");
      Serial.println(TempRead/SensorReading[4].Vin, 4);
      
      SensorReading[4].Vin = TempRead / (float)doc["Value"];
      
      if (Debug) {
        Serial.print("SensorReading[4].Vin = ");
        Serial.println(SensorReading[4].Vin, 4);
        Serial.print("Volt(nachher) = ");
        Serial.println(TempRead/SensorReading[4].Vin, 4);
      }

      preferences.begin("Jeepify", false);
      preferences.putFloat("Vin", SensorReading[4].Vin);
      preferences.end();
    }
    if ((doc["Order"] == "SetSensor0Sens") and (doc.containsKey("Sens0"))) {
      TempSens = (float) doc["Sens0"];
      preferences.begin("Jeepify", false);
      preferences.putFloat("Sens0", TempSens);
      preferences.end();
    }
    if ((doc["Order"] == "SetSensor1Sens") and (doc.containsKey("Sens1"))) {
      TempSens = (float) doc["Sens1"];
      preferences.begin("Jeepify", false);
      preferences.putFloat("Sens1", TempSens);
      preferences.end();
    }
    if ((doc["Order"] == "SetSensor2Sens") and (doc.containsKey("Sens2"))) {
      TempSens = (float) doc["Sens2"];
      preferences.begin("Jeepify", false);
      preferences.putFloat("Sens2", TempSens);
      preferences.end();
    }
    if ((doc["Order"] == "SetSensor3Sens") and (doc.containsKey("Sens3"))) {
      TempSens = (float) doc["Sens3"];
      preferences.begin("Jeepify", false);
      preferences.putFloat("Sens3", TempSens);
      preferences.end();
    }
    if (doc["Order"]   == "Reset")          { ClearMonitors(); }
    if (doc["Order"]   == "Restart")        { ESP.restart(); }
    if (doc["Order"]   == "Pair")           { TimestampPair = millis(); ReadyToPair = true; }
    if (doc["Pairing"] == "you are paired") { 
      for (int i = 0; i < 6; i++ ) TempBroadcast[i] = (uint8_t) mac[i];
      
      bool exists = esp_now_is_peer_exist(TempBroadcast);
      if (exists) { 
        printMAC(TempBroadcast); Serial.println(" already exists...");
        ReadyToPair = false;
      }
      else {
        bool PairingSuccess = false;
        for (int zNr=0; zNr<MAX_MONITOR; zNr++) {
          if ((M[zNr].Type == 0) and (!PairingSuccess)) {
            M[zNr].Type = MONITOR_ROUND;
            for (int b = 0; b < 6; b++ ) M[zNr].BroadcastAddress[b] = TempBroadcast[b];
            M[zNr].TimestampLastSeen = millis();
            
            esp_now_add_peer(M[zNr].BroadcastAddress, ESP_NOW_ROLE_COMBO, 0, NULL, 0);
            Serial.print("Adding in slot: "); Serial.println(zNr);
            Serial.print("Monitor: "); Serial.print(M[zNr].MonitorName);
            Serial.print(" (");printMAC(M[zNr].BroadcastAddress); Serial.println(")\n");
            
            PairingSuccess = true; 
            if (PAIR_AUTO_STOP == 1) ReadyToPair = false;
            SaveMonitors();
          }
        }
        if (!PairingSuccess) { printMAC(TempBroadcast); Serial.println(" adding failed..."); } 
      }
    }
  }
  else {
        Serial.print(F("deserializeJson() failed: "));  //Just in case of an ERROR of ArduinoJSon
        Serial.println(error.f_str());
        return;
  }
}

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.print("Last Packet Send Status: ");
  if (sendStatus == 0){
    Serial.println("Delivery success");
    TimestampLastContact = millis();
    Serial.print("LastContact (send succes): "); Serial.println(TimestampLastContact);
  }
  else{
    Serial.println("Delivery fail");
  }
}

void setup() {
  Serial.begin(74880);
  TimestampLastContact = 0;
  
  Serial.println("Initialisierung beginn...");
//ClearMonitors();

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  GetMonitors();
  ReportMonitors();
  RegisterMonitors();

  if (AnzMonitor == 0) {
    ReadyToPair = true;
    TimestampPair = millis();
  }
  else ReadyToPair = false;
  
  preferences.begin("Jeepify", true);
  
  SensorReading[0].Name     = NAME_SENSOR_0;
  SensorReading[0].Type     = 1;
  SensorReading[0].IOPort   = 0;
  SensorReading[0].NullWert = preferences.getInt("Nullwert0", 3134);
  SensorReading[0].VperAmp  = preferences.getFloat("Sens0", 0.066);

  SensorReading[1].Name     = NAME_SENSOR_1;
  SensorReading[1].Type     = 1;
  SensorReading[1].IOPort   = 1;
  SensorReading[1].NullWert = preferences.getInt("Nullwert1", 3134);
  SensorReading[1].VperAmp  = preferences.getFloat("Sens1", 0.066);

  SensorReading[2].Name     = NAME_SENSOR_2;
  SensorReading[2].Type     = 1;
  SensorReading[2].IOPort   = 2;
  SensorReading[2].NullWert = preferences.getInt("Nullwert2", 3150);
  SensorReading[2].VperAmp  = preferences.getFloat("Sens2", 0.066);

  SensorReading[3].Name     = NAME_SENSOR_3;
  SensorReading[3].Type     = 1;
  SensorReading[3].IOPort   = 3;
  SensorReading[3].NullWert = preferences.getInt("Nullwert3", 3150);
  SensorReading[3].VperAmp  = preferences.getFloat("Sens3", 0.066);

  SensorReading[4].Name     = NAME_SENSOR_4;
  SensorReading[4].Type     = 2;
  SensorReading[4].IOPort   = PIN_A0;
  SensorReading[4].Vin      = preferences.getInt("Vin", 200);
  SensorReading[4].VperAmp  = 1;

  SleepMode = preferences.getBool("SleepMode", true);

  preferences.end();

  Wire.begin(D5, D6);

  ads.setGain(GAIN_TWOTHIRDS);  // 0.1875 mV/Bit .... +- 6,144V
  ads.begin();

  Serial.println("Initialisierung fertig...");
}

void loop() {
  if ((millis() > WAIT_FOR_MAMA) and (TimestampLastContact == 0)) {
    Serial.println("Going to sleep...");
    
    Serial.print("Millis: "); Serial.println(millis());
    Serial.print("LastContact: "); Serial.println(TimestampLastContact);
    Serial.print("SleepMode: "); Serial.println(SleepMode);

    if (SleepMode == true) ESP.deepSleep(SLEEP_DURATION);
  } 
  
  if ((millis() - TimestampLastContact > SLEEP_AFTER) and (TimestampLastContact > 0)) {
    Serial.println("Going to sleep... after Contact");
    
    Serial.print("Millis: "); Serial.println(millis());
    Serial.print("LastContact: "); Serial.println(TimestampLastContact);
    Serial.print("SleepMode: "); Serial.println(SleepMode);

    if (SleepMode == true) ESP.deepSleep(SLEEP_DURATION);
  }

  if ((ReadyToPair == false) and  (millis() - TimestampSend > 5000)) {
    TimestampSend = millis();
    SendMessage();
  }
  if ((ReadyToPair == true) and  (millis() - TimestampSend > 1000)) {
    TimestampSend = millis();
    SendPairingReuest();
    if  (millis() - TimestampPair > TIME_TO_PAIR) ReadyToPair = false;
  }
}
void printMAC(const uint8_t * mac_addr){
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
}