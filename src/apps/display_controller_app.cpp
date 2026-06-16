#include <Arduino.h>
#include "display_controller_app.h"

// CrowPanel low-level e-paper driver (vendored, see lib/CrowPanelEPD/ATTRIBUTION.md).
// EPD.h pulls in EPD_Init.h + spi.h, giving us:
//   Paint_NewImage / Paint_Clear / EPD_Draw* / EPD_ShowString   (drawing into a buffer)
//   EPD_GPIOInit / EPD_Init / EPD_Display / EPD_Update / EPD_DeepSleep  (panel transport)
//   EPD_W (800), EPD_H (272), WHITE (0xFF), BLACK (0x00), Rotation (0)
#include "EPD.h"

// Mission 1 progress:
//   [x] step 1 — project skeleton + role dispatch
//   [>] step 2 — bring up the panel: render a diagnostic frame & push it (this file)
//   [ ] step 3 — GFXcanvas1(800,272) + renderMain/Detail/Actuators(canvas, data)
//   [ ] step 4 — Menu/Exit page switching + rotary focus + CONF toggle (page 3)

// Board-level: the CrowPanel gates the e-paper power rail behind GPIO 7. It MUST
// be driven HIGH before any EPD access, otherwise the panel stays unpowered, keeps
// its previous image, and silently ignores all SPI. (Confirmed against Elecrow's
// official example + the cubic9com reference.)
static const int EPD_POWER_PIN = 7;

// Full-panel 1-bit framebuffer in the driver's native 800x272 memory layout
// (the extra 8 columns are the dead zone between the two SSD1683 controllers).
// 800 * 272 / 8 = 27200 bytes.
static uint8_t s_frame[EPD_W * EPD_H / 8];

// A deliberately asymmetric test image so we can verify orientation (not rotated),
// handedness (not mirrored), the inter-controller junction near x=396, and contrast.
static void renderBringUpTest() {
  Paint_NewImage(s_frame, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);

  // Frame + filled title bar (drawing space is logical 0..791 x 0..271).
  EPD_DrawRectangle(0, 0, 791, 271, BLACK, 0);
  EPD_DrawRectangle(0, 0, 791, 38, BLACK, 1);

  char line[56];
  EPD_ShowString(10, 8, (char *)"ESP-CLAW  DISPLAY NODE", 24, WHITE);

  EPD_ShowString(10, 58, (char *)"Panel bring-up OK", 24, BLACK);
  snprintf(line, sizeof(line), "%s   node %d", DEVICE_NAME, NODE_ID);
  EPD_ShowString(10, 92, line, 24, BLACK);
  EPD_ShowString(10, 126, (char *)"792 x 272  1-bit  B/W", 24, BLACK);

  // Landmarks for orientation/mirror/junction checks:
  EPD_ShowString(10, 235, (char *)"BL", 24, BLACK);            // bottom-left tag
  EPD_ShowString(760, 235, (char *)"BR", 24, BLACK);           // bottom-right tag
  EPD_DrawLine(0, 38, 120, 158, BLACK);                        // diagonal from top-left
  EPD_DrawCircle(700, 150, 34, BLACK, 0);                      // hollow circle, right side
  EPD_DrawLine(396, 40, 396, 269, BLACK);                      // vertical at the controller seam
}

void display_controller_setup() {
  Serial.println();
  Serial.println(F("=== ESP-Claw :: Display Controller ==="));
  Serial.printf("Node:   %d\n", NODE_ID);
  Serial.printf("Device: %s\n", DEVICE_NAME);

  Serial.println(F("EPD: enabling panel power rail (GPIO 7)"));
  pinMode(EPD_POWER_PIN, OUTPUT);
  digitalWrite(EPD_POWER_PIN, HIGH);
  delay(100);  // let the e-paper supply settle before init

  Serial.println(F("EPD: GPIO init"));
  EPD_GPIOInit();

  Serial.println(F("EPD: rendering diagnostic frame into buffer"));
  renderBringUpTest();

  // A proper full init + a full white clear pass FIRST removes ghosting and gives
  // deep blacks; then write the image and show it. Matches the known-good reference
  // sequence: FastMode1Init -> Display_Clear -> Update -> Display -> PartUpdate.
  Serial.println(F("EPD: fast-mode init + full white clear"));
  EPD_FastMode1Init();
  EPD_Display_Clear();
  EPD_Update();

  Serial.println(F("EPD: writing image + full-refresh show"));
  EPD_Display(s_frame);
  EPD_Update();  // full waveform = deep black (partial update renders gray/faint)
  EPD_DeepSleep();

  Serial.println(F("EPD: done — frame latched, panel asleep"));
}

void display_controller_loop() {
  // Static image; nothing to drive until we add rendering + input (steps 3-4).
  delay(1000);
}
