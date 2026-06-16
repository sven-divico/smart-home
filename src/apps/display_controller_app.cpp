#include <Arduino.h>
#include "display_controller_app.h"

// CrowPanel low-level e-paper driver (vendored) — TRANSPORT only. All drawing
// goes through our portable Canvas1 UI layer.
#include "EPD.h"
// The driver #defines BLACK/WHITE as macros, which would clobber Canvas1's member
// constants of the same name. We only need the driver's transport here.
#undef BLACK
#undef WHITE
#include "ui/canvas1.h"
#include "ui/ui_model.h"
#include "ui/pages.h"

// Mission 1 progress:
//   [x] step 1 — skeleton + role dispatch   [x] step 2 — first pixels (clean)
//   [x] step 3 — Canvas1 + the 3 page renderers, on hardware (180° rotated)
//   [>] step 4 — navigation: Menu/Exit page switching + rotary focus + CONF toggle

// The CrowPanel gates the e-paper power rail behind GPIO 7 — HIGH before any EPD
// access or the panel stays unpowered and silently ignores SPI.
static const int EPD_POWER_PIN = 7;

// Onboard controls (active-low, use INPUT_PULLUP — pressed reads LOW).
static const uint8_t PIN_EXIT = 1;   // Exit button  -> next page
static const uint8_t PIN_MENU = 2;   // Menu button  -> previous page
static const uint8_t PIN_DOWN = 4;   // rotary down  -> focus next (page 3)
static const uint8_t PIN_CONF = 5;   // rotary press -> toggle focused pump (page 3)
static const uint8_t PIN_UP   = 6;   // rotary up    -> focus prev (page 3)

// Full-panel framebuffer in the driver's native 800x272 layout (== Canvas1::FRAME_BYTES).
static uint8_t s_frame[EPD_W * EPD_H / 8];

// Runtime UI state (mutable copy of the mock model so CONF can flip pump states).
static UiModel g_model;
static int     g_page  = 0;   // 0 = Main, 1 = Detail, 2 = Actuators
static int     g_focus = 0;   // highlighted pump row on Page 3

enum { PAGE_MAIN, PAGE_DETAIL, PAGE_ACTUATORS, PAGE_COUNT };

// --- debounced active-low button (fires once per press) ---------------------
struct Button { uint8_t pin; bool rawPressed; bool stable; uint32_t tChange; };
static Button bMenu{PIN_MENU, false, false, 0};
static Button bExit{PIN_EXIT, false, false, 0};
static Button bUp  {PIN_UP,   false, false, 0};
static Button bDown{PIN_DOWN, false, false, 0};
static Button bConf{PIN_CONF, false, false, 0};

static bool pressed(Button &b) {
  bool raw = (digitalRead(b.pin) == LOW);      // active-low -> LOW means pressed
  uint32_t now = millis();
  if (raw != b.rawPressed) { b.rawPressed = raw; b.tChange = now; }
  if (now - b.tChange >= 25 && raw != b.stable) {  // settled for 25 ms
    b.stable = raw;
    if (raw) return true;                          // just transitioned to pressed
  }
  return false;
}

// --- panel transport --------------------------------------------------------
// Two refresh modes:
//   * FULL  (page changes / periodic scrub): FastMode1Init + Display + FastUpdate.
//     One fast pass — low flicker, ghost-free. Leaves the panel awake + initialised.
//   * PARTIAL (in-page changes: cursor move, toggle): Display + PartUpdate (0xDC),
//     no re-init/reset — only changed pixels transition, so no full-screen flash.
// The panel is kept awake (no DeepSleep) so a partial can follow a full. Every
// FULL_EVERY partials we force a full refresh to scrub any ghost buildup.
static const int FULL_EVERY = 12;
static int s_partialsSinceFull = 0;

static void renderCurrentPage() {
  Canvas1 canvas(s_frame);
  switch (g_page) {
    case PAGE_MAIN:      renderMain(canvas, g_model);          break;
    case PAGE_DETAIL:    renderDetail(canvas, g_model);        break;
    case PAGE_ACTUATORS: renderActuators(canvas, g_model, g_focus); break;
  }
  canvas.rotate180();  // panel is mounted upside-down; sim stays upright
}

static void pushFull() {
  // Ghost-free but lighter: keep the white-clear pass (erases the previous page,
  // which a single pass can't do reliably after partials) but drive both passes
  // with the FAST waveform (0xC7) instead of FULL (0xF7) — fewer inversions per
  // pass, so noticeably less flicker while staying ghost-free.
  EPD_FastMode1Init();
  EPD_Display_Clear();
  EPD_FastUpdate();        // fast white clear — erases the previous page
  EPD_Display(s_frame);
  EPD_FastUpdate();        // fast image
  s_partialsSinceFull = 0;
}

static void pushPartial() {
  if (s_partialsSinceFull >= FULL_EVERY) { pushFull(); return; }  // periodic scrub
  EPD_Display(s_frame);
  EPD_PartUpdate();
  s_partialsSinceFull++;
}

static void drawFull()    { renderCurrentPage(); pushFull(); }
static void drawPartial() { renderCurrentPage(); pushPartial(); }

static int countActivePumps() {
  int n = 0;
  for (int i = 0; i < 4; i++) if (g_model.pumps[i].active) n++;
  return n;
}

void display_controller_setup() {
  Serial.println();
  Serial.println(F("=== ESP-Claw :: Display Controller ==="));
  Serial.printf("Node: %d  Device: %s\n", NODE_ID, DEVICE_NAME);

  pinMode(PIN_EXIT, INPUT_PULLUP);
  pinMode(PIN_MENU, INPUT_PULLUP);
  pinMode(PIN_DOWN, INPUT_PULLUP);
  pinMode(PIN_CONF, INPUT_PULLUP);
  pinMode(PIN_UP,   INPUT_PULLUP);

  g_model = mockModel();

  Serial.println(F("EPD: enabling panel power rail (GPIO 7)"));
  pinMode(EPD_POWER_PIN, OUTPUT);
  digitalWrite(EPD_POWER_PIN, HIGH);
  delay(100);
  EPD_GPIOInit();

  Serial.println(F("Nav: Menu=prev  Exit=next  |  Page 3: wheel Up/Down=focus, CONF=toggle"));
  drawFull();  // initial render (Main)
}

void display_controller_loop() {
  // Poll every button each pass so debouncers stay current across page changes.
  bool eMenu = pressed(bMenu), eExit = pressed(bExit);
  bool eUp = pressed(bUp), eDown = pressed(bDown), eConf = pressed(bConf);

  bool pageChanged = false;   // Menu/Exit -> whole screen changes -> FULL refresh
  bool inPageChanged = false; // Page-3 cursor/toggle -> small change -> PARTIAL refresh

  if (eMenu) { g_page = (g_page + PAGE_COUNT - 1) % PAGE_COUNT; pageChanged = true; }
  if (eExit) { g_page = (g_page + 1) % PAGE_COUNT; pageChanged = true; }

  if (g_page == PAGE_ACTUATORS && !pageChanged) {  // wheel is live only on the actuator page
    if (eUp)   { g_focus = (g_focus + 4 - 1) % 4; inPageChanged = true; }
    if (eDown) { g_focus = (g_focus + 1) % 4; inPageChanged = true; }
    if (eConf) {
      Pump &p = g_model.pumps[g_focus];
      p.on = !p.on;
      p.active = p.on;                     // reflect running state on the row
      g_model.activePumps = countActivePumps();
      inPageChanged = true;
    }
  }

  if (pageChanged) {
    Serial.printf("nav: page=%d (full)\n", g_page);
    drawFull();
  } else if (inPageChanged) {
    Serial.printf("nav: focus=%d (partial)\n", g_focus);
    drawPartial();
  }
  delay(5);
}
