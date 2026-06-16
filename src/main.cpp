#include <Arduino.h>
#include "app_select.h"

// main.cpp stays minimal on purpose: it only boots the serial port and hands
// off to the role-specific app selected at compile time (see app_select).
void setup() {
  Serial.begin(115200);
  delay(300);
  app_setup();
}

void loop() {
  app_loop();
}
