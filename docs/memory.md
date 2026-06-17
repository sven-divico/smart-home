# Project Memory — ESP-Claw Garden System

Running log of decisions and state. Newest first.

## 2026-06-17 — Mission 2: local time-series storage ✅ (RAM-validated; SD slot defective on this unit)
Replaced the mock `UiModel` with a real time-series data layer, built via subagent-driven development (9 chunks, each spec + code-quality reviewed, plus a whole-system review).
- **Architecture:** pure host-testable core in `lib/TimeSeries/` (`Ring` → `Series` [raw 15-min/48h + daily/400d, downsample-on-write] → `TimeSeriesStore` [registry + queries: latest/sampleWindow/averageAcrossNodes/minMaxToday/trend] · `Clock` [soft, injected millis] · `PumpLog` [run pairing, dangling-START, audit fields] · `SeriesStorage` iface + `InMemoryStorage` · `SimSource`). Device glue in `src/data/`: `SdStorage` (binary ring files on µSD, device-only) + `GardenRepository` (builds `UiModel` — the **cloud-ready seam**; later swap for a `CachedCloudRepository`, UI unchanged).
- **Data model:** binary fixed records `{uint32 ts; int16 value}` (6 B), fixed-point per metric (temp ×10, lux ÷4, rest ×1). Multi-resolution rings, downsample-on-write. Pump events `{ts,pumpId,event,soilPct,thresholdPct}` (self-contained audit). 30s fine sampling while pumping rolls up into the 15-min raw average (control vs storage separated).
- **Persistence flow:** seed (`pushRawDirect`, no sink) + `persistAll` → reload (`pushRawDirect`, no sink) → live `tick` (`add`, fires sink → SD). Write-through via a function-pointer sink on Series/PumpLog; RAM-only fallback when no SD. Soft clock persisted to SD.
- **Verified:** 30 native Unity tests (`pio test -e native_test`, suites in `test/`); host preview renders the real 3 pages from the simulated store (`sim/preview.sh`); device boots, seeds ~30 d, renders all 3 pages on the panel (RAM-only). RAM 58% / Flash 12%.
- **Whole-system review caught** a cross-component bug: `SimSource` private pump flags desynced from a reloaded `PumpLog` on reboot-while-watering → duplicate START; fixed by making the log the single source of truth. Open follow-ups (task chips): SD pump-log reload should read newest-256 not oldest; first-boot seed→SD `persistAll` isn't crash-safe (consider bulk `saveRing` + a `seeded.flag`).
- **SD bring-up + DEFECT:** µSD is its own SPI bus — CS **10**, SCK **39**, MOSI **40**, MISO **13** (Elecrow wiki/example), driven on a dedicated `SPIClass(HSPI)`; needs `gpio_pullup_en(MISO)` + power rail (GPIO 7) enabled before `SD.begin`. 64 GB cards ship exFAT → must reformat **FAT32** (`diskutil eraseDisk FAT32 …`). **This unit's SD slot is defective:** a known-good FAT32 card (mounts on Mac) fails SPI init (`no token` / `Card Failed cmd 0x37/0x29` / `APP_OP_COND failed: 255`) even with all of the above + pull-ups on MISO/MOSI/CS — internal MISO pull-up only gets CMD0 through, CMD55 still fails → bad MISO line/joint. Warranty report: `docs/sd-slot-defect-report-2026-06-17.md`. SD firmware is correct; will work on a healthy board.

## 2026-06-16 — Navigation + partial refresh (Mission 1, step 4) ✅ — Mission 1 DONE
Buttons drive the UI on the panel; in-page changes use partial refresh.
- **Inputs:** Exit=IO1 (next page), Menu=IO2 (prev page), rotary Up=IO6 / Down=IO4 / CONF=IO5. All `INPUT_PULLUP`, **active-low**, 25 ms debounce (`pressed()` edge detector). Confirmed working on hardware.
- **Nav model:** Menu/Exit cycle Main↔Detail↔Actuator. On Page 3 the wheel moves a **selection cursor** and CONF toggles the focused pump (mutable `g_model` copy; `activePumps` recomputed).
- **Refresh strategy (tuned live with Sven):**
  - Page changes → `pushFull()` = `FastMode1Init → Display_Clear → FastUpdate → Display → FastUpdate`. The **white-clear pass is required** to erase the previous page (a single pass ghosts — particle persistence, esp. after partials). Both passes use the **fast** waveform (0xC7): Sven chose **lowest flicker** over deepest black (full 0xF7 = crisper but more flicker). Trade is real, his call.
  - In-page Page-3 changes (cursor move, toggle) → `pushPartial()` = `Display + EPD_PartUpdate` (0xDC), no re-init → only the changed strip transitions, **no full-screen flash**. Every `FULL_EVERY`=12 partials a full refresh scrubs ghost buildup.
  - **No DeepSleep** anymore (panel kept awake so a partial can follow a full).
- **Page 3 layout reworked for clean partials:** columns are now `cursor gutter | toggle | name | moisture | mode | timing`. Cursor + toggle are the two adjacent leftmost columns, so a focus or toggle change touches ONE small contiguous left strip → crisp partial. The name no longer shifts with focus (that was leaving partial artifacts). Dropped: per-row status text, the black running edge bar, and the EIN/AUS label (state = toggle graphic alone). CONF hint moved to the static footer.
- **Deferred polish (Sven, "later"):** general visual polish of all 3 pages; decide whether to restore the EIN/AUS toggle label; tiny Picopixel `%` legibility; possible deeper partial-refresh (old-RAM/windowed) if the occasional dimness bothers later.

## 2026-06-16 — Display UI rendering (Mission 1, step 3) ✅ — all 3 pages on hardware
All three pages render (Main/Detail/Actuators), flashed + validated on the CrowPanel. Seam invisible + contrast good on real e-paper; rendering "reasonably good", final polish deferred by Sven. Decisions:
- **3c/3d:** `renderDetail` (env row + 4 sensor tiles w/ truncating names, EIN/AUS badges, soil%+temp/"trocken", 12 h+7 d sparklines on a shared per-tile scale) and `renderActuators(c,m,focus)` (5-col pump grid: rounded toggle, moisture-vs-Ziel bar w/ target tick, AUTO+reason, timing; running row = black left-edge + ▶, focused row = 2px ring; wheel active in rail). All in `src/ui/pages.cpp`.
- **Device wiring:** `display_controller_app.cpp` wraps `s_frame` in a `Canvas1`, renders the page, then `pushFrame()` (proven FastMode1Init→Clear→Update→Display→Update→DeepSleep). Loop cycles the 3 pages every 7 s (temporary until step-4 nav).
- **180° rotation:** panel is mounted upside-down. `Canvas1::rotate180()` (in-place full-bitstream reversal; maps the centred seam onto itself; verified involution + visually) is called **device-only** after render — the **sim stays upright** (what we design against). 
- **Gotchas:** the driver `#define`s `BLACK`/`WHITE`, colliding with `Canvas1::BLACK/WHITE` — `#undef` both after `EPD.h` before the UI headers in the device app. Serial port enumerated as **`/dev/cu.usbserial-110`** this session (memory said `-10`; CH340 suffix drifts — check `ls /dev/cu.*`). To watch serial use `~/.platformio/penv/bin/python` (has pyserial; miniconda doesn't).

## 2026-06-16 — Display UI rendering (Mission 1, step 3a–3b) ✅
Host-renderable UI pipeline built and Page 1 (Main) done + polished. Decisions:
- **Render core (chose B over Adafruit GFX):** our own portable `Canvas1` 1-bit surface (`src/ui/canvas1.*`) drawing into the driver's *native* 800×272 buffer layout (bit 1=white, bit 0=ink, seam at logical x≥396 → +8). So no blit: render into `s_frame` on device, dump same buffer to PGM on host. Zero Arduino deps → compiles native with no shim.
- **Fonts:** reuse Adafruit GFXfont *data* only (MIT) in `src/ui/fonts/` (FreeSerifBold 24/18/12, FreeSerif9, FreeSans/Bold9, Picopixel) via a minimal vendored `gfxfont.h`; their `#include <Adafruit_GFX.h>` is repointed to it. Our own ~40-line glyph renderer in Canvas1.
- **UTF-8 layer in Canvas1:** synthesizes glyphs the ASCII fonts lack — `°` (ring), `·` (mid-dot), `ä/ö/ü/Ä/Ö/Ü` (base+diaeresis), `ß`→ss, `Ø/ø` (O+slash). Plus a `track` (letter-spacing) param — the small bold fonts have ~0 side bearing so adjacent digits touch without it.
- **Host preview:** `env:host_sim` (`platform = native`) compiles only `src/ui/**` + `src/host_preview.cpp` (device app excluded via `build_src_filter`, `CrowPanelEPD` lib_ignored). `sim/preview.sh` = build + render `build/preview/*.pgm` + `pgm2png.py`. Iterate layouts on the Mac, ~1 s/cycle, no flashing. (Gotcha: don't include `<string>`/`<algorithm>` in host code — Apple clang vs SDK libc++ `__builtin_ctzg` mismatch; use C strings.)
- **platformio.ini restructured:** `[env]` now holds only target-agnostic `build_flags`; platform/framework/upload moved into `[env:display_controller]` so the native env can inherit `[env]` cleanly.
- **Page 1 sign-off:** matches the locked mockup (masthead, hero 22°+sun kept crisp at 24pt per Sven, ↗ pressure, dual-axis chart w/ outlined bars + haloed temp line, ✓ message band, footer, right rail). `renderMain(Canvas1&, const UiModel&)` in `src/ui/pages.cpp`; mock data `src/ui/ui_model.*` (spec §6).

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

## ▶ RESUME NEXT SESSION — Missions 1 & 2 DONE
Mission 2 merged to `main`: real time-series data layer behind a cloud-ready repository, validated on hardware (RAM-only — this unit's SD slot is defective, warranty report filed; SD code is correct for a healthy board). Dev loop unchanged: edit code, `sim/preview.sh`, view `build/preview/*@2x.png`; native tests `pio test -e native_test`; flash `pio run -e display_controller -t upload --upload-port /dev/cu.usbserial-10` (port suffix drifts — `ls /dev/cu.*`). Options next:
1. **Get a board with a working SD slot** (or repair this one) and verify SD persistence end-to-end (reboot keeps history; pump-event round-trip).
2. **Follow-up chips:** SdStorage pump-log reload newest-256; crash-safe first-boot seeding.
3. **Mission 3 candidates:** ESP-NOW + a real sensor node (replace `SimSource` with live readings), or polish pass on the 3 pages.

## Missions
- [x] **Mission 1:** PlatformIO foundation + 3 pages w/ mock data + navigation, deployed to CrowPanel. ✅
  - [x] step 1 — skeleton + role dispatch · [x] step 2 — first pixels (clean) · [x] step 3 — UI rendering (Main/Detail/Actuator on hardware, 180° rotation) · [x] step 4 — navigation (inputs + page switching + Page-3 cursor/CONF + partial refresh)
- [x] **Mission 2:** data structures + time-series storage. ✅ (RAM-validated on hardware; SD persistence blocked by a defective SD slot on this unit — code correct, warranty report `docs/sd-slot-defect-report-2026-06-17.md`)
  - [x] ch1 metrics/test env · [x] ch2 Ring · [x] ch3 Series · [x] ch4 Store+queries · [x] ch5 Clock+PumpLog · [x] ch6 persistence (iface/InMemory/SD/write-through) · [x] ch7 SimSource · [x] ch8 Repository+preview · [x] ch9 device integration
- [ ] Later: ESP-NOW, sensor/pump/gateway nodes, Telegram, HA/ESPHome.
