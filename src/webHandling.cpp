

#define DEBUG_WIFI(m) Serial.print(m)

#include <Arduino.h>
#include <ArduinoOTA.h>

#include <ESP8266WiFi.h>      

#include <time.h>
//needed for library
#include <DNSServer.h>

#include <IotWebConf.h>
#include <IotWebConfUsing.h> // This loads aliases for easier class names.
#include <IotWebConfTParameter.h>

#include "common.h"
#include "statusHandling.h"

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "12345678";

// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "B1"

// -- When CONFIG_PIN is pulled to ground on startup, the Thing will use the initial
//      password to buld an AP. (E.g. in case of lost password)
#define CONFIG_PIN  -1

// -- Status indicator pin.
//      First it will light up (kept LOW), on Wifi connection it will blink,
//      when connected to the Wifi it will turn off (kept HIGH).
#define STATUS_PIN LED_BUILTIN

// -- Method declarations.
void handleRoot();
void convertParams();

// -- Callback methods.
void configSaved();
bool formValidator(iotwebconf::WebRequestWrapper*);

DNSServer dnsServer;
WebServer server(80);

bool gParamsChanged = true;

uint16_t gCapacityAh;

uint16_t gChargeEfficiencyPercent;

uint16_t gMinPercent;

uint16_t gTailCurrentmA;

uint16_t gFullVoltagemV;

uint16_t gFullDelayS;

float gShuntResistancemR;

uint16_t gMaxCurrentA;

uint16_t gModbusId;


// -- We can add a legend to the separator
IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);

IotWebConfParameterGroup sysConfGroup = IotWebConfParameterGroup("SysConf","Sensor");

iotwebconf::FloatTParameter shuntResistance =
   iotwebconf::Builder<iotwebconf::FloatTParameter>("shuntR").
   label("Shunt resistance [m&#8486;]").
   defaultValue(0.75).
   step(0.01).
   placeholder("e.g. 0.75").
   build();

iotwebconf::UIntTParameter<uint16_t> maxCurrent =
  iotwebconf::Builder<iotwebconf::UIntTParameter<uint16_t>>("maxA").
  label("Expected max current [A]").
  defaultValue(200).
  min(1).
  step(1).
  placeholder("1..65535").
  build();


IotWebConfParameterGroup shuntGroup = IotWebConfParameterGroup("ShuntConf","Smart shunt");

iotwebconf::UIntTParameter<uint16_t> battCapacity =
  iotwebconf::Builder<iotwebconf::UIntTParameter<uint16_t>>("battAh").
  label("Battery capacity [Ah]").
  defaultValue(100).
  min(1).
  step(1).
  placeholder("1..65535").
  build();

iotwebconf::UIntTParameter<uint16_t> chargeEfficiency =
  iotwebconf::Builder<iotwebconf::UIntTParameter<uint16_t>>("cheff").
  label("Charge efficiency [%]").
  defaultValue(95).
  min(1).
  max(100).
  step(1).
  placeholder("1..100").
  build();

iotwebconf::UIntTParameter<uint16_t> minSoc =
  iotwebconf::Builder<iotwebconf::UIntTParameter<uint16_t>>("minsoc").
  label("Minimun SOC [%]").
  defaultValue(10).
  min(1).
  max(100).
  step(1).
  placeholder("1..100").
  build();

IotWebConfParameterGroup fullGroup = IotWebConfParameterGroup("FullD","Full detection");

iotwebconf::UIntTParameter<uint16_t> tailCurrent =
  iotwebconf::Builder<iotwebconf::UIntTParameter<uint16_t>>("tailC").
  label("Tail current [mA]").
  defaultValue(1000).
  min(1).
  step(1).
  placeholder("1..65535").
  build();


iotwebconf::UIntTParameter<uint16_t> fullVoltage =
  iotwebconf::Builder<iotwebconf::UIntTParameter<uint16_t>>("fullV").
  label("Voltage when full [mV]").
  defaultValue(55200).
  min(1).
  step(1).
  placeholder("1..65535").
  build();

iotwebconf::UIntTParameter<uint16_t> fullDelay =
  iotwebconf::Builder<iotwebconf::UIntTParameter<uint16_t>>("fullDelay").
  label("Delay before full [s]").
  defaultValue(30).
  min(1).
  step(1).
  placeholder("1..65535").
  build();


IotWebConfParameterGroup modbusGroup = IotWebConfParameterGroup("modbus","Modbus settings");
iotwebconf::UIntTParameter<uint16_t> modbusId =
  iotwebconf::Builder<iotwebconf::UIntTParameter<uint16_t>>("mbid").
  label("Modbus Id").
  defaultValue(2).
  min(1).
  max(128).
  step(1).
  placeholder("1..128").
  build();


void wifiSetShuntVals() {
    shuntResistance.value() = gShuntResistancemR;
    maxCurrent.value() = gMaxCurrentA;
}

void wifiSetModbusId() {
    modbusId.value() = gModbusId;
}

void wifiStoreConfig() {
    iotWebConf.saveConfig();
}


void wifiConnected()
{
   ArduinoOTA.begin();
}

void wifiSetup()
{
  
  sysConfGroup.addItem(&shuntResistance);
  sysConfGroup.addItem(&maxCurrent);

  shuntGroup.addItem(&battCapacity);
  shuntGroup.addItem(&chargeEfficiency);
  shuntGroup.addItem(&minSoc);

  fullGroup.addItem(&fullVoltage);
  fullGroup.addItem(&tailCurrent);
  fullGroup.addItem(&fullDelay);
  
  modbusGroup.addItem(&modbusId);
  
  iotWebConf.setStatusPin(STATUS_PIN);
  iotWebConf.setConfigPin(CONFIG_PIN);

  iotWebConf.addParameterGroup(&sysConfGroup);
  iotWebConf.addParameterGroup(&shuntGroup);
  iotWebConf.addParameterGroup(&fullGroup);
  iotWebConf.addParameterGroup(&modbusGroup);

  iotWebConf.setConfigSavedCallback(&configSaved);
  iotWebConf.setWifiConnectionCallback(&wifiConnected);


  iotWebConf.setFormValidator(formValidator);
  iotWebConf.getApTimeoutParameter()->visible = true;

  // -- Initializing the configuration.
  iotWebConf.init();
  
  Serial.println("Converting params");
  convertParams();
  Serial.println("Values are:");
  Serial.printf("Resistance: %.2f\n",gShuntResistancemR);
  Serial.printf("Capacity %df\n",gCapacityAh);
  Serial.printf("MOdbus ID %df\n",gModbusId);
  

  // -- Set up required URL handlers on the web server.
  server.on("/", handleRoot);
  server.on("/config", [] { iotWebConf.handleConfig(); });
  server.onNotFound([]() { iotWebConf.handleNotFound(); });
}

void wifiLoop()
{
  // -- doLoop should be called as frequently as possible.
  iotWebConf.doLoop();
  ArduinoOTA.handle();

/*
  if(gNeedReset) {
      Serial.println("Rebooting after 1 second.");
      iotWebConf.delay(1000);
      ESP.restart();
  }
  */
}

/**
 * Handle web requests to "/" path.
 */
void handleRoot()
{
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal())
  {
    // -- Captive portal request were already served.
    return;
  }

  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";

  s += "<title>INR based smart shunt</title></head><body>";
  
  s += "<br><br><b>Config Values</b> <ul>";
  s += "<li>Shunt resistance  : "+String(gShuntResistancemR);
  s += "<li>Shunt max current : "+String(gMaxCurrentA);
  s += "<li>Batt capacity     : "+String(gCapacityAh);
  s += "<li>Batt efficiency   : "+String(gChargeEfficiencyPercent);
  s += "<li>Min soc           : "+String(gMinPercent);
  s += "<li>Tail current      : "+String(gTailCurrentmA);
  s += "<li>Batt full voltage : "+String(gFullVoltagemV);
  s += "<li>Batt full delay   : "+String(gFullDelayS);
  s += "<li>Modbus ID         : "+String(gModbusId);
  s += "</ul><hr><br>";
  
  s += "<br><br><b>Dynamic Values</b> <ul>";
  s += "<li>Battery Voltage: "+String(gBattery.voltage());
  s += "<li>Shunt current  : "+String(gBattery.current());
  s += "<li>Avg current    : "+String(gBattery.averageCurrent());
  s += "<li>Battery soc    : "+String(gBattery.soc());
  s += "<li>Time to go     : "+String(gBattery.tTg());
  s += "<li>Battery full   : "+String(gBattery.isFull());
  s += "</ul>";
  s += "Go to <a href='config'>configure page</a> to change configuration.";
  s += "</body></html>\n";

  server.send(200, "text/html", s);
}


void convertParams() {
    gShuntResistancemR = shuntResistance.value();
    gMaxCurrentA = maxCurrent.value();
    gCapacityAh = battCapacity.value();
    gChargeEfficiencyPercent = chargeEfficiency.value();
    gMinPercent = minSoc.value();
    gTailCurrentmA = tailCurrent.value();
    gFullVoltagemV = fullVoltage.value();
    gFullDelayS = fullDelay.value();
    gModbusId = modbusId.value();
}

void configSaved()
{ 
  convertParams();
  gParamsChanged = true;
} 



bool formValidator(iotwebconf::WebRequestWrapper* webRequestWrapper)
{ 
  bool result = true;

  int l = 0;

  
    if (server.arg(shuntResistance.getId()).toFloat() <=0)
    {
        shuntResistance.errorMessage = "Shunt resistance has to be > 0";
        result = false;
    }

    l = server.arg(maxCurrent.getId()).toInt();
    if ( l <= 0)
    {
        maxCurrent.errorMessage = "Maximal current must be > 0";
        result = false;
    }

    l = server.arg(battCapacity.getId()).toInt();
    if (l <= 0)
    {
        battCapacity.errorMessage = "Battery capacity must be > 0";
        result = false;
    }

    l = server.arg(chargeEfficiency.getId()).toInt();
    if (l <= 0  || l> 100) {
        chargeEfficiency.errorMessage = "Charge efficiency must be between 1% and 100%";
        result = false;
    }


    l = server.arg(minSoc.getId()).toInt();
    if (l <= 0  || l> 100) {
        minSoc.errorMessage = "Minimum SOC must be between 1% and 100%";
        result = false;
    }

    l = server.arg(tailCurrent.getId()).toInt();
    if (l < 0 ) {
        tailCurrent.errorMessage = "Tail current must be > 0";
        result = false;
    }

    l = server.arg(fullVoltage.getId()).toInt();
    if (l < 0 ) {
        fullVoltage.errorMessage = "Voltage when full must be > 0";
        result = false;
    }

    l = server.arg(fullDelay.getId()).toInt();
    if (l < 0 ) {
        fullDelay.errorMessage = "Delay before full must be > 0";
        result = false;
    }

  return result;
  }
