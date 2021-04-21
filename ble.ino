#include <BLEPeripheral.h>
#include "nrf_nvic.h"

#include "types.h"
#include "pins.h"
#include "blink.h"

// How long to wait after the last button press before shutting down the system.
static const uint32_t kShutdownDelayMs = 3 * 60 * 60 * 1000;

volatile bool buttonPressFlag = false;
void buttonInterruptHandler() {
  buttonPressFlag = true;
}

BLEPeripheral blePeripheral = BLEPeripheral();

// create service
BLEService ledService = BLEService("89a8591dbb19485b9f5958492bc33e24");

// create switch characteristic
BLECharCharacteristic switchCharacteristic = BLECharCharacteristic("894c8042e841461ca5c95a73d25db08e", BLERead | BLENotify);

// State transition and executes exit action for the current state.
StateEnum stateTransition(StateContainer *sc, Event e) {
  StateEnum newState = STATE_INVALID;
  switch (sc->state) {
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
	if (sc->state != newState) {
		sc->stateEnteredMs = millis();
	}
	sc->state = newState;
	return newState;
}

StateContainer sc;

Event getEvents(StateContainer *sc) {
	Event e = EVENT_NONE;
	if (buttonPressFlag) {
		buttonPressFlag = false;
		e = static_cast<Event>(e | Event(EVENT_BUTTON_DOWN));
	}
	if (digitalRead(BUTTON_PIN) == HIGH) {
		e = static_cast<Event>(e | Event(EVENT_BUTTON_UP));
	}
	if ((millis() - sc->stateEnteredMs) > kShutdownDelayMs) {
		e = static_cast<Event>(e | Event(EVENT_HOURS_3));
	}
	if ((millis() - sc->stateEnteredMs) > 10) {
		e = static_cast<Event>(e | Event(EVENT_MILLIS_10));
	}
	if (blePeripheral.connected()) {
		e = static_cast<Event>(e | Event(EVENT_CONNECTED));
	}

  return e;
}

void loop() {
	blinkQuick();
	Event e = getEvents(&sc);
	stateTransition(&sc, e);

	// Perform the actions upon entering the (new) state.
	switch (sc.state) {
	case STATE_OFF:
		// Do a Power OFF, this is the last statement executed.
		NRF_POWER->SYSTEMOFF = 1;
	case STATE_BROADCAST:
		// Keep sleeping (system ON) until a connection established.
		sd_app_evt_wait();
		// See https://www.iot-experiments.com/nrf51822-and-ble400/
		sd_nvic_ClearPendingIRQ(SWI2_IRQn);
		break;
	case STATE_SLEEP:
		// Keep sleeping (system ON) until a button is pressed.
		sd_app_evt_wait();
		// See https://www.iot-experiments.com/nrf51822-and-ble400/
		sd_nvic_ClearPendingIRQ(SWI2_IRQn);
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

	// Do the BLE things.
	blePeripheral.poll();
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);

  // Button press signals wake up. See https://github.com/sandeepmistry/arduino-nRF5/issues/243
  NRF_GPIO->PIN_CNF[BUTTON_PIN] &= ~((uint32_t)GPIO_PIN_CNF_SENSE_Msk);
  NRF_GPIO->PIN_CNF[BUTTON_PIN] |= ((uint32_t)GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos);
	// Button needs a pull up not to be noisy.
  NRF_GPIO->PIN_CNF[BUTTON_PIN] &= ~((uint32_t)GPIO_PIN_CNF_PULL_Msk);
  NRF_GPIO->PIN_CNF[BUTTON_PIN] |= ((uint32_t)GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos);
    
  // set advertised local name and service UUID
  blePeripheral.setLocalName("100% PTT");
  blePeripheral.setAdvertisedServiceUuid(ledService.uuid());
  blePeripheral.setAdvertisingInterval(1000);

  // add service and characteristic
  blePeripheral.addAttribute(ledService);
  blePeripheral.addAttribute(switchCharacteristic);

  // assign event handlers for connected, disconnected to peripheral
  blePeripheral.setEventHandler(BLEConnected, blePeripheralConnectHandler);
  blePeripheral.setEventHandler(BLEDisconnected, blePeripheralDisconnectHandler);
  // begin initialization
  blePeripheral.begin();
  switchCharacteristic.setValue(0);
  
  // enable low power mode and interrupt
  sd_power_mode_set(NRF_POWER_MODE_LOWPWR);
  attachInterruptLowAccuracy(digitalPinToInterrupt(BUTTON_PIN), buttonInterruptHandler, FALLING);

  sc.state = STATE_OFF;
}

void blePeripheralConnectHandler(BLECentral& central) {
}

void blePeripheralDisconnectHandler(BLECentral& central) {
}
