#include "types.h"
#include "nrf_soc.h"
#include "nrf_nvic.h"
#include <SPI.h>
#include <BLEPeripheral.h>
#include <nrf.h>

static const uint8_t BUTTON_PIN = 28;
static const uint8_t LED_PIN = 29;

// How long to wait after the last button press before shutting down the system.
static const uint32_t kShutdownDelayMs = 3 * 60 * 60 * 1000;

volatile uint8_t buttonValue = 0;
volatile bool buttonPressFlag = false;
volatile uint32_t lastPressMs = 0;

void buttonInterruptHandler() {
  buttonValue = digitalRead(BUTTON_PIN);
  buttonPressFlag = true;
  lastPressMs = millis();
}

BLEPeripheral blePeripheral = BLEPeripheral();

// create service
BLEService ledService = BLEService("89a8591dbb19485b9f5958492bc33e24");

// create switch characteristic
BLECharCharacteristic switchCharacteristic = BLECharCharacteristic("894c8042e841461ca5c95a73d25db08e", BLERead | BLENotify);

#ifdef TIME_PROP
BLEUnsignedIntCharacteristic    timeCharacteristic = BLEUnsignedIntCharacteristic("894c8042e841461ca5c95a73d25db08f", BLERead | BLENotify);
#endif

StateEnum stateTransition(StateEnum s, Event e) {
  StateEnum newState = STATE_INVALID;
  switch (s) {
	case STATE_OFF:
	  newState = STATE_BROADCAST;
		break;
	case STATE_BROADCAST:
		if (e & EVENT_CONNECTED) {
		  newState = STATE_SLEEP;
		} else {
		  newState = STATE_BROADCAST;
		}
	  break;
	case STATE_SLEEP:
		if (e & EVENT_BUTTON_DOWN) {
		  newState = STATE_ENSURE_DOWN;
		} else if (e & EVENT_HOURS_3) {
		  newState = STATE_OFF;
		} else {
		  newState = STATE_SLEEP;
		}
	  break;
	case STATE_ENSURE_DOWN:
	  if (e & EVENT_MILLIS_10) {
		  newState = STATE_TRANSMIT;
		} else {
		  newState = STATE_SLEEP;
		}
	  break;
	case STATE_TRANSMIT:
		if (e & EVENT_BUTTON_UP) {
		  newState = STATE_ENSURE_UP;
			digitalWrite(LED_PIN, LOW);
		} else {
		  newState = STATE_TRANSMIT;
		}
	  break;
	case STATE_ENSURE_UP:
		if (e & EVENT_BUTTON_DOWN) {
		  newState = STATE_TRANSMIT;
		} else {
		  newState = STATE_SLEEP;
			switchCharacteristic.setValue(0);
		}
	  break;
	default:
		blinkToDeath();
	}
	return newState;
}

void l() {
	uint32_t wakeUpTime = millis();
	uint32_t stateEnteredMs = millis();
  StateEnum s = STATE_OFF;

	while (true) {
		blinkQuick();
	  Event e = EVENT_NONE;
		if (buttonPressFlag) {
			buttonPressFlag = false;
		  e = static_cast<Event>(e | Event(EVENT_BUTTON_DOWN));
		}
		if (digitalRead(BUTTON_PIN) == HIGH) {
		  e = static_cast<Event>(e | Event(EVENT_BUTTON_UP));
		}
		if ((millis() - stateEnteredMs) > kShutdownDelayMs) {
		  e = static_cast<Event>(e | Event(EVENT_HOURS_3));
		}
		if ((millis() - stateEnteredMs) > 10) {
		  e = static_cast<Event>(e | Event(EVENT_MILLIS_10));
		}
		if (blePeripheral.connected()) {
		  e = static_cast<Event>(e | Event(EVENT_CONNECTED));
		}

	  StateEnum newS = stateTransition(s, e);
		// Perform the actions upon entering the new state.
		switch (newS) {
		case STATE_OFF:
			// Do a Power OFF, this is the last statement executed.
			NRF_POWER->SYSTEMOFF = 1;
		case STATE_BROADCAST:
		  // Keep sleeping (system ON) until a connection established.
			sd_app_evt_wait();
			// See https://www.iot-experiments.com/nrf51822-and-ble400/
			sd_nvic_ClearPendingIRQ(SWI2_IRQn);
			wakeUpTime = millis();
			break;
		case STATE_SLEEP:
		  // Keep sleeping (system ON) until a button is pressed.
			sd_app_evt_wait();
			// See https://www.iot-experiments.com/nrf51822-and-ble400/
			sd_nvic_ClearPendingIRQ(SWI2_IRQn);
			wakeUpTime = millis();
			break;
		case STATE_ENSURE_DOWN:
			break;
		case STATE_TRANSMIT:
			switchCharacteristic.setValue(1);
			digitalWrite(LED_PIN, HIGH);
			break;
		case STATE_ENSURE_UP:
			break;
		default:
		  blinkToDeath();
			break;
		}
		if (s != newS) {
		  stateEnteredMs = millis();
		}
		s = newS;

		// Do the BLE things.
		blePeripheral.poll();
	}
}

void blinkQuick() {
	digitalWrite(LED_PIN, LOW);
	delay(10);
	digitalWrite(LED_PIN, HIGH);
	delay(50);
	digitalWrite(LED_PIN, LOW);
}

void blinkFunny() {
  while(true) {
		digitalWrite(LED_PIN, 1 - digitalRead(LED_PIN));
	  delay(100);
	}
}

void blinkToDeath() {
  while(true) {
		digitalWrite(LED_PIN, 1 - digitalRead(LED_PIN));
	  delay(300);
	}
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);

  // Button press signals wake up. See https://github.com/sandeepmistry/arduino-nRF5/issues/243
  NRF_GPIO->PIN_CNF[BUTTON_PIN] &= ~((uint32_t)GPIO_PIN_CNF_SENSE_Msk);
  NRF_GPIO->PIN_CNF[BUTTON_PIN] |= ((uint32_t)GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos);
  NRF_GPIO->PIN_CNF[BUTTON_PIN] &= ~((uint32_t)GPIO_PIN_CNF_PULL_Msk);
  NRF_GPIO->PIN_CNF[BUTTON_PIN] |= ((uint32_t)GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos);
    
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
  attachInterruptLowAccuracy(digitalPinToInterrupt(BUTTON_PIN), buttonInterruptHandler, FALLING);
}

void loop() {
  l();
	// Keep CPU running while the button is pressed.
  if (digitalRead(BUTTON_PIN) == LOW) {
		// SYSTEM ON sleep. Known reasons to exit:
		// - RTC1 which overflows every 512 seconds.
		// - Button presses.
		sd_app_evt_wait();
		// See https://www.iot-experiments.com/nrf51822-and-ble400/
		sd_nvic_ClearPendingIRQ(SWI2_IRQn);
  }
  if (buttonPressFlag) {
    switchCharacteristic.setValue(1-buttonValue);
		buttonPressFlag = false;
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
