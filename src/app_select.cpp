#include "app_select.h"

// Compile-time role dispatch. Add an #elif branch per role as we build them out
// (sensor node, pump node, gateway, main controller). Mission 1 only needs the
// display controller.
#if defined(ROLE_DISPLAY_CONTROLLER)
  #include "apps/display_controller_app.h"
  void app_setup() { display_controller_setup(); }
  void app_loop()  { display_controller_loop(); }
#else
  #error "No device role defined — set a -D ROLE_* flag in platformio.ini"
#endif
