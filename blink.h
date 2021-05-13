#ifndef BLINK_H_
  #include "pins.h"

void blinkQuick() {
  digitalWrite(LED_PIN, LOW);
  delay(5);
  digitalWrite(LED_PIN, HIGH);
  delay(10);
  digitalWrite(LED_PIN, LOW);
}

void blinkFunny() {
  while (true) {
    digitalWrite(LED_PIN, 1 - digitalRead(LED_PIN));
    delay(100);
  }
}

void blinkToDeath() {
  while (true) {
    digitalWrite(LED_PIN, 1 - digitalRead(LED_PIN));
    delay(300);
  }
}
  #define BLINK_H_
#endif  // BLINK_H_
