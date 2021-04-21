#include "nrf_soc.h"
#include "nrf_nvic.h"
#include <SPI.h>
#include <BLEPeripheral.h>
#include <nrf.h>

static const uint8_t BUTTON_PIN = 28;
static const uint8_t LED_PIN = 29;

// How long to wait after the last button press before shutting down the system.
static const uint32_t kShutdownDelayMs = 3 * 60 * 60 * 1000;

//custom boards may override default pin definitions with BLEPeripheral(PIN_REQ, PIN_RDY, PIN_RST)
BLEPeripheral                    blePeripheral                            = BLEPeripheral();

// create service
//BLEService               ledService           = BLEService("19b10000e8f2537e4f6cd104768a1214");
BLEService               ledService           = BLEService("89a8591dbb19485b9f5958492bc33e24");

// create switch characteristic
//BLECharCharacteristic    switchCharacteristic = BLECharCharacteristic("19b10001e8f2537e4f6cd104768a1214", BLERead | BLEWrite);
BLECharCharacteristic    switchCharacteristic = BLECharCharacteristic("894c8042e841461ca5c95a73d25db08e", BLERead | BLENotify);

#ifdef TIME_PROP
BLEUnsignedIntCharacteristic    timeCharacteristic = BLEUnsignedIntCharacteristic("894c8042e841461ca5c95a73d25db08f", BLERead | BLENotify);
#endif

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);

  // Button press signals wake up. See https://github.com/sandeepmistry/arduino-nRF5/issues/243
  NRF_GPIO->PIN_CNF[BUTTON_PIN] &= ~((uint32_t)GPIO_PIN_CNF_SENSE_Msk);
  NRF_GPIO->PIN_CNF[BUTTON_PIN] |= ((uint32_t)GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos);
    
  // set advertised local name and service UUID
  blePeripheral.setLocalName("100pct-PTT");
  blePeripheral.setAdvertisedServiceUuid(ledService.uuid());
  blePeripheral.setAdvertisingInterval(1000);

  // add service and characteristic
  blePeripheral.addAttribute(ledService);
  blePeripheral.addAttribute(switchCharacteristic);
#ifdef TIME_PROP
  blePeripheral.addAttribute(timeCharacteristic);
#endif

  // assign event handlers for connected, disconnected to peripheral
  blePeripheral.setEventHandler(BLEConnected, blePeripheralConnectHandler);
  blePeripheral.setEventHandler(BLEDisconnected, blePeripheralDisconnectHandler);
  // begin initialization
  blePeripheral.begin();
  switchCharacteristic.setValue(0);
#ifdef TIME_PROP
  timeCharacteristic.setValue(0);
#endif
  
  // enable low power mode and interrupt
  sd_power_mode_set(NRF_POWER_MODE_LOWPWR);
  attachInterruptLowAccuracy(digitalPinToInterrupt(BUTTON_PIN), but, RISING);
}

volatile uint8_t buttonValue = 0;
volatile bool sendValue = false;
volatile uint32_t lastPressMs = 0;

void but() {
  if (buttonValue != digitalRead(BUTTON_PIN)) {
	buttonValue = digitalRead(BUTTON_PIN);
	sendValue = true;
	lastPressMs = millis();
  }
  // Change the sense mode to detect both rising and falling. Can't find how to
  // catch TOGGLE from the PORT event.
  auto mode = RISING;
  if (digitalRead(BUTTON_PIN)) {
	mode = FALLING;
  }
  attachInterruptLowAccuracy(digitalPinToInterrupt(BUTTON_PIN), but, mode);
}

void loop() {
  // SYSTEM ON sleep. Known reasons to exit:
  // - RTC1 which overflows every 512 seconds.
  // - Button presses.
  sd_app_evt_wait();
  // See https://www.iot-experiments.com/nrf51822-and-ble400/
  sd_nvic_ClearPendingIRQ(SWI2_IRQn);
  if (sendValue) {
    switchCharacteristic.setValue(1-buttonValue);
	sendValue = false;
#ifdef TIME_PROP
	timeCharacteristic.setValue(lastW);
#endif
  }
  if (millis() - lastPressMs >= kShutdownDelayMs) {
	// Power OFF
	NRF_POWER->SYSTEMOFF = 1;
  }
  // poll peripheral
  blePeripheral.poll();
}

void blePeripheralConnectHandler(BLECentral& central) {
  //digitalWrite(LED_PIN, HIGH);
}

void blePeripheralDisconnectHandler(BLECentral& central) {
  //digitalWrite(LED_PIN, LOW);
}
