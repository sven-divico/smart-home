# Project Memory — ESP-Claw Garden System

Running log of decisions and state. Newest first.

## 2026-06-16 — Display bring-up (Mission 1, step 2) ✅
First pixels on the CrowPanel. Two non-obvious gotchas, both now in code:
1. **GPIO 7 = e-paper power-enable.** Must `pinMode(7,OUTPUT); digitalWrite(7,HIGH)` BEFORE `EPD_GPIOInit()`. Without it the panel is unpowered, keeps its old image, and silently ignores SPI (no hang — BUSY reads low).
2. **Refresh sequence matters for ghosting + contrast.** Use the full path, not basic `EPD_Init()`/partial update:
   `EPD_FastMode1Init()` → `EPD_Display_Clear()` → `EPD_Update()` (white clear, kills ghost) → `EPD_Display(buf)` → `EPD_Update()` (FULL, deep black). Partial update (`EPD_PartUpdate`) renders gray/faint — only for small incremental changes later.
- EPD pins confirmed against Elecrow official == vendored: SCK 12, MOSI 11, RES 47, DC 46, CS 45, BUSY 48. Framebuffer 800×272/8 = 27200 bytes (8-px dead seam at x=396 handled by the Paint layer).
- CH340 bridge (VID 1A86:7523) on `/dev/cu.usbserial-10`; **upload at 460800** (921600 fails "No serial data received").
- Build/flash: `~/.platformio/penv/bin/pio run -e display_controller -t upload --upload-port /dev/cu.usbserial-10`.

## 2026-06-16 — UI design locked
- Reviewed vision (`esp-claw-project-vision-platformio.md`). Confirmed hardware: CrowPanel ESP32 5.79" e-paper, **792×272, 1-bit B/W**, SSD1683 (partial refresh), ESP32-S3 8MB/8MB, onboard rotary + Menu + Exit buttons, microSD, battery.
- Designed the 3-page display UI via visual brainstorming. Spec: `docs/specs/2026-06-16-display-ui-design.md`.
- Chosen aesthetic: **editorial B/W**. Chart: combined dual-axis (outlined bars = soil Ø, line = temp w/ halo).
- Navigation: **Menu = previous page, Exit = next page, rotary wheel = in-page interaction**; right control rail; page dots. Page 3 (pumps) is interactive (rotate to focus, CONF to toggle).
- Layout rules: fixed-grid alignment (content never pushes dividers); overflow names truncate with `…`; alert band inverts to black for warnings.
- Rendering architecture: draw into Adafruit `GFXcanvas1`, blit to panel on device / dump to PNG on host. Panel-driver library TBD (GxEPD2 vs Elecrow lib) — decide in Mission 1.

## Conventions
- Specs → `docs/specs/`  ·  Plans → `docs/plans/`  ·  This log → `docs/memory.md`
- Locale: German labels, DD.MM.YYYY, 24h, metric.
- Mock UI data values are fixed in the spec §6 — reuse them consistently.

## ▶ RESUME NEXT SESSION — Mission 1, step 3
First pixels work (step 2 done). Next: real UI rendering.
1. Add `lib_deps = adafruit/Adafruit GFX Library` to the `display_controller` env.
2. Build a **canvas→panel bridge**: draw a page into a `GFXcanvas1(792,272)` (white=bit 1, ink=bit 0), copy into `s_frame` reusing `Paint_SetPixel` (handles the 8-px seam), push with the proven sequence `EPD_FastMode1Init → EPD_Display_Clear → EPD_Update → EPD_Display(s_frame) → EPD_Update`.
3. Add a mock data model (spec §6 values) + `renderMain()` first (editorial dashboard), then Detail, then Actuators. FreeSerifBold/FreeSans (bundled with Adafruit GFX) approximate the editorial type.
Then step 4 = navigation (Menu=prev, Exit=next, rotary focus + CONF toggle on page 3).
Replace `renderBringUpTest()` in `src/apps/display_controller_app.cpp` with the real renderers.

## Missions
- [~] **Mission 1:** PlatformIO foundation + 3 pages w/ mock data + navigation, deployed to CrowPanel.
  - [x] step 1 — skeleton + role dispatch · [x] step 2 — first pixels (clean) · [ ] step 3 — GFXcanvas1 UI · [ ] step 4 — navigation
- [ ] **Mission 2:** data structures + time-series storage (SD/LittleFS).
- [ ] Later: ESP-NOW, sensor/pump/gateway nodes, Telegram, HA/ESPHome.
