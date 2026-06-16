#pragma once

// Every node role implements these two entry points. The role is chosen at
// compile time via a -D ROLE_* build flag in platformio.ini; app_select.cpp
// wires the generic app_setup()/app_loop() to the right role implementation.
void app_setup();
void app_loop();
