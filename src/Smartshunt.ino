#include <Arduino.h>
#include <Wire.h>
//#include <INA226.h>

#include "common.h"
#include "sensorHandling.h"
#include "webHandling.h"
#include "modbusHandling.h"
#include "victronHandling.h"

void setup() {
  // put your setup code here, to run once:
#if ARDUINO_USB_CDC_ON_BOOT
    SERIAL_VICTRON.begin(19200,SERIAL_8N1,RX,TX);
    SERIAL_VICTRON.setPins(RX, TX, -1, -1);
    SERIAL_DBG.begin(115200);
#else
    SERIAL_DBG.begin(19200);
#endif
    
    wifiSetup();

#if (SOC_UART_NUM > 1)
    SERIAL_MODBUS.begin(9600, SERIAL_8N2);
#endif

    sensorInit();
    modbusInit();
    victronInit();
}

void loop() {
  // put your main code here, to run repeatedly:
  wifiLoop();
    if (gParamsChanged) {
        modbusInit();
        victronInit();
    }
    sensorLoop();
    modbusLoop();
    victronLoop();
    gParamsChanged = false;
}
