#include <BLEPeripheral.h>

#include "blink.h"
#include "nrf_nvic.h"
#include "pins.h"
#include "types.h"

// How long to wait after the last button press before shutting down the system
// from the connected state.
static const uint32_t kDisconnectAndShutdownDelayMs = 6 * 60 * 60 * 1000;
// for xalps buttons:
//static const uint32_t kDisconnectAndShutdownDelayMs = 300 * 60 * 60 * 1000;
// How long to wait after the last button press before shutting down the system
// from the advertising state.
static const uint32_t kShutdownDelayMs = 1 * 60 * 1000;

// Frequency of BT advertising transmissions.
static const uint32_t kAdvertisingIntervalMs = 60;
// min, max connection interval in 1.25ms units. Lower values make the button more responsive.
static const uint32_t kConnectionIntervalMin = 36;  // 45ms
static const uint32_t kConnectionIntervalMax = 36;

volatile bool buttonPressFlag = false;
void buttonInterruptHandler() { buttonPressFlag = true; }

// Used for reading battery voltage. Taken from
// https://os.mbed.com/users/MarceloSalazar/notebook/measuring-battery-voltage-with-nordic-nrf51x/
uint16_t readVoltageAdc(void) {
  NRF_ADC->ENABLE = ADC_ENABLE_ENABLE_Enabled;
  NRF_ADC->CONFIG =
      (ADC_CONFIG_RES_10bit << ADC_CONFIG_RES_Pos) |
      (ADC_CONFIG_INPSEL_SupplyOneThirdPrescaling << ADC_CONFIG_INPSEL_Pos) |
      (ADC_CONFIG_REFSEL_VBG << ADC_CONFIG_REFSEL_Pos) |
      (ADC_CONFIG_PSEL_Disabled << ADC_CONFIG_PSEL_Pos) |
      (ADC_CONFIG_EXTREFSEL_None << ADC_CONFIG_EXTREFSEL_Pos);

  NRF_ADC->CONFIG &= ~ADC_CONFIG_PSEL_Msk;
  NRF_ADC->CONFIG |= ADC_CONFIG_PSEL_Disabled << ADC_CONFIG_PSEL_Pos;
  NRF_ADC->TASKS_START = 1;
  while (((NRF_ADC->BUSY & ADC_BUSY_BUSY_Msk) >> ADC_BUSY_BUSY_Pos) ==
         ADC_BUSY_BUSY_Busy) {
  };
  uint16_t result = static_cast<uint16_t>(NRF_ADC->RESULT);  // 10 bit
  NRF_ADC->ENABLE = ADC_ENABLE_ENABLE_Disabled;
  return result;
}

uint8_t getBatLevel() {
    float volts = readVoltageAdc() * 3.6f / 1024.0f;

    // https://uncannier.com/cr2032-lithium-battery-charge-level-measurement/
    const struct Point { float v; uint16_t c; } vcPairs[] = { 
        {3.0, 100}, {2.9, 80}, {2.8, 60}, {2.7, 40}, 
        {2.6, 30}, {2.5, 20}, {2.4, 10}, {2.0, 0} 
    };

    if (volts >= vcPairs[0].v) return 100;
    if (volts <= vcPairs[7].v) return 0;

    for (int i = 1; i < 8; i++) {
        if (volts > vcPairs[i].v) {
            const Point &p1 = vcPairs[i-1];
            const Point &p2 = vcPairs[i];
            return round(p1.c + (volts - p1.v) * (p2.c - p1.c)/(p2.v - p1.v));
        }
    }
    return 0;
}

BLEPeripheral blePeripheral = BLEPeripheral();

// ffe0 is what a ptt button that worked on an iphone used
BLEService buttonService("ffe0");
// TODO: not sure what this is for, but it seems to make it work on an iphone.
BLEUnsignedCharCharacteristic iphoneCharacteristic("1525", BLERead | BLEWrite);
BLECharCharacteristic buttonCharacteristic("ffe1", BLERead | BLENotify);

BLEService batteryService("180f");
BLEUnsignedCharCharacteristic batteryLevel("2a19", BLERead | BLENotify);

// State transition and executes exit action for the current state.
// Diagram: https://photos.app.goo.gl/6E5oTZQ78MnhBJMR8
StateEnum stateTransition(StateContainer* sc, Event e) {
  StateEnum newState = STATE_INVALID;
  switch (sc->state) {
    case STATE_OFF:
      if (e & EVENT_BUTTON_DOWN) {
        newState = STATE_BROADCAST;
      } else {
        newState = STATE_OFF;
      }
      break;
    case STATE_BROADCAST:
      if (e & EVENT_CONNECTED) {
        newState = STATE_SLEEP;
      } else if (e & EVENT_MINUTES_10) {
        newState = STATE_OFF;
      } else {
        newState = STATE_BROADCAST;
      }
      break;
    case STATE_SLEEP:
      if (e & EVENT_BUTTON_DOWN) {
        newState = STATE_ENSURE_DOWN;
      } else if (e & EVENT_SHUTDOWN_TIMEOUT) {
        newState = STATE_OFF;
      } else if (e & EVENT_DISCONNECTED) {
        newState = STATE_BROADCAST;
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
        buttonCharacteristic.setValue(0);
        batteryLevel.setValue(getBatLevel());
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

Event getEvents(StateContainer* sc) {
  Event e = EVENT_NONE;
  if (buttonPressFlag) {
    buttonPressFlag = false;
    e = static_cast<Event>(e | Event(EVENT_BUTTON_DOWN));
  }
  if (digitalRead(BUTTON_PIN) == HIGH) {
    e = static_cast<Event>(e | Event(EVENT_BUTTON_UP));
  }
  if ((millis() - sc->stateEnteredMs) > kDisconnectAndShutdownDelayMs) {
    e = static_cast<Event>(e | Event(EVENT_SHUTDOWN_TIMEOUT));
  }
  if ((millis() - sc->stateEnteredMs) > kShutdownDelayMs) {
    e = static_cast<Event>(e | Event(EVENT_MINUTES_10));
  }
  if ((millis() - sc->stateEnteredMs) > 10) {
    e = static_cast<Event>(e | Event(EVENT_MILLIS_10));
  }
  if (blePeripheral.connected()) {
    e = static_cast<Event>(e | Event(EVENT_CONNECTED));
  } else {
    e = static_cast<Event>(e | Event(EVENT_DISCONNECTED));
  }

  return e;
}

void loop() {
  blinkQuick();
  Event e = getEvents(&sc);
  StateEnum oldState = sc.state;
  stateTransition(&sc, e);

  // Perform the actions upon entering the (new) state.
  switch (sc.state) {
    case STATE_OFF:
      // Do a Power OFF, this is the last statement executed.
      NRF_POWER->SYSTEMOFF = 1;
    case STATE_BROADCAST:
      // Keep sleeping (system ON) until a connection
      // established.
      sd_app_evt_wait();
      // See
      // https://www.iot-experiments.com/nrf51822-and-ble400/
      sd_nvic_ClearPendingIRQ(SWI2_IRQn);
      break;
    case STATE_SLEEP:
      // Keep sleeping (system ON) until a button is pressed.
      sd_app_evt_wait();
      // See
      // https://www.iot-experiments.com/nrf51822-and-ble400/
      sd_nvic_ClearPendingIRQ(SWI2_IRQn);
      break;
    case STATE_ENSURE_DOWN:
      break;
    case STATE_TRANSMIT:
      if (oldState != sc.state) {
        buttonCharacteristic.setValue(1);
      }
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

  digitalWrite(LED_PIN, HIGH);
  delay(100);
  digitalWrite(LED_PIN, LOW);
  delay(500);
  // Button press signals wake up. See
  // https://github.com/sandeepmistry/arduino-nRF5/issues/243
  NRF_GPIO->PIN_CNF[BUTTON_PIN] &= ~((uint32_t)GPIO_PIN_CNF_SENSE_Msk);
  NRF_GPIO->PIN_CNF[BUTTON_PIN] |=
      ((uint32_t)GPIO_PIN_CNF_SENSE_Low << GPIO_PIN_CNF_SENSE_Pos);
  // Button needs a pull up not to be noisy.
  NRF_GPIO->PIN_CNF[BUTTON_PIN] &= ~((uint32_t)GPIO_PIN_CNF_PULL_Msk);
  NRF_GPIO->PIN_CNF[BUTTON_PIN] |=
      ((uint32_t)GPIO_PIN_CNF_PULL_Pullup << GPIO_PIN_CNF_PULL_Pos);

  // set advertised local name and service UUID
  uint8_t mfg_data[] = {0x59,0,0,0x95};
  blePeripheral.setManufacturerData(mfg_data, 4);
  blePeripheral.setLocalName("CLICK");
  blePeripheral.setDeviceName("CLICK");
  blePeripheral.setAppearance(BLE_APPEARANCE_GENERIC_REMOTE_CONTROL);
  blePeripheral.setAdvertisingInterval(kAdvertisingIntervalMs);
  blePeripheral.setConnectionInterval(kConnectionIntervalMin, kConnectionIntervalMax);

  // add service and characteristic
  blePeripheral.setAdvertisedServiceUuid(buttonService.uuid());
  blePeripheral.addAttribute(buttonService);
  blePeripheral.addAttribute(buttonCharacteristic);
  blePeripheral.addAttribute(iphoneCharacteristic);
  
  blePeripheral.setAdvertisedServiceUuid(batteryService.uuid());
  blePeripheral.addAttribute(batteryService);
  blePeripheral.addAttribute(batteryLevel);

  // begin initialization
  blePeripheral.begin();
  buttonCharacteristic.setValue(0);
  batteryLevel.setValue(getBatLevel());
  iphoneCharacteristic.setValue(1);

  // enable low power mode and interrupt
  sd_power_mode_set(NRF_POWER_MODE_LOWPWR);
  attachInterruptLowAccuracy(digitalPinToInterrupt(BUTTON_PIN),
   buttonInterruptHandler, FALLING);

  sc.state = STATE_OFF;
}

void blePeripheralConnectHandler(BLECentral& central) {
  iphoneCharacteristic.setValue(1);
}

void blePeripheralDisconnectHandler(BLECentral& central) {}
