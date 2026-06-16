# Display UI Design — CrowPanel 5.79" E-Paper

**Status:** Approved (2026-06-16)
**Scope:** The indoor e-paper display node UI for the ESP-Claw garden system. Three pages rendered with mock data, navigated with the panel's physical controls.
**Out of scope (deferred):** real sensor data, ESP-NOW transport, time-series storage, the other node roles (sensor/pump/gateway/controller).

Visual mockups (to scale, 1-bit) live in `.superpowers/brainstorm/95896-1781597748/`:
- `main-c-final.html` — Page 1
- `page2-detail-rich.html` — Page 2
- `page3-actuator-switch.html` — Page 3
- `navigation-all-screens.html` — all three with final navigation chrome

---

## 1. Hardware constraints (drive every design decision)

| Property | Value |
|---|---|
| Panel | Elecrow CrowPanel ESP32 5.79" E-Paper HMI (DIS08792E) |
| Resolution | **792 × 272 px**, landscape, ~2.9:1 |
| Color | **1-bit black & white only** — no grayscale, no color |
| Driver IC | SSD1683 — supports full + **partial refresh** |
| MCU | ESP32-S3, 240 MHz, **8 MB flash, 8 MB PSRAM** |
| Inputs | **Rotary switch** (Up=IO6, Down=IO4, CONF/press=IO5), **Menu** button (IO2), **Exit** button (IO1), Reset, Boot |
| Storage | microSD (SPI), battery + charge circuit |
| Free GPIO | IO3, IO9, IO15, IO17, IO19, IO21 |

Implications: design with strong contrast, bold type, line work, hatch/outline fills (no gray fills). E-paper refresh is slow (~1–2 s full), so favor partial refresh and avoid per-second animation.

## 2. Design language

- **Editorial / "newspaper weather page"** aesthetic. Serif display type (headlines, big numbers) + sans-serif (Helvetica-like) for labels, axes, and data chrome.
- **Icons are custom 1-bit bitmaps** we draw/ship ourselves (weather sun/cloud/rain/moon, ✓ check, ⚠ warning, water drop). **No emoji** — they render as color and won't survive on a 1-bit panel.
- Fills are **outline or hatch**, never solid gray. Inversion (white-on-black) is reserved for emphasis (active states, warnings).

## 3. Navigation model (global)

- **Menu** button (top-right) → **previous** page
- **Exit** button (bottom-right) → **next** page
- **Rotary wheel** (Up/Down + CONF press) → **in-page interaction** (freed from page-switching)
- A slim **right-edge control rail (~34 px)** is always visible, mirroring the physical buttons: `▲ MENÜ zurück` top, `▼ EXIT weiter` bottom, wheel role in the middle (`Rad frei` when idle, `◉ drehen / wählen / CONF` when active). Content area is therefore **758 px** wide.
- **Page dots** (`● ○ ○`) in each page header show current position (3 pages).
- Pages cycle (Main → Detail → Actuator → Main).

## 4. Cross-cutting layout rules

1. **Fixed-grid alignment:** multi-row layouts (e.g. Page 2 env row vs node tiles) share the same fixed column x-positions. Content is clipped to its cell and **must never push a divider**. (In firmware this is automatic — we draw at fixed coordinates.)
2. **Text truncation:** first-column / name text that overflows its cell is trimmed with a trailing `…`.
3. **Alert inversion:** the message band shows calm state normally (outlined, `✓ keine aktuellen Meldungen`); a real warning **inverts to black** (`⚠ STURMWARNUNG …`) so it dominates.
4. **Locale:** German labels, `DD.MM.YYYY`, 24 h clock, metric units (°C, %, hPa, lux).

## 5. Pages

### Page 1 — Main (`DER GARTEN`)
At-a-glance ambient/editorial dashboard.
- **Masthead:** title `DER GARTEN` left, full date + time right, page dots.
- **Left block:** big current air temp `22°` + weather glyph; condition line (`Sonnig · gefühlt 24°`); **pressure tendency** — bold arrow (`↗`/`→`/`↘`) + `1013 hPa` + trend words. Barometric tendency = the weather-change indicator.
- **Right block:** **combined dual-axis chart** in a frame — soil moisture Ø as **outlined bars** + air temperature as a **line with a white knockout halo** (so it reads over the bars), shared 7-day x-axis, °C left axis, % right axis, dashed mid gridline, day labels.
- **Message band** (reclaimed strip): alerts; calm `✓ keine aktuellen Meldungen`, inverts for warnings.
- **Footer stat line:** `58% Feuchte · 1013 hPa · 12k lux · Beete: 41·37·52·29%`.
- Wheel idle on this page.

### Page 2 — Detail (`DETAIL · SENSOREN`)
- **Header:** title, page dots, clock.
- **Environment row** (full width, 4 equal cells): Lufttemperatur, Luftfeuchte, Luftdruck (+ tendency arrow), Licht.
- **4 node tiles** (equal columns, same grid as env row): per bed — **name (truncates) · pump badge (filled EIN / outlined AUS) · current soil moisture % (+ small soil temp) · 12 h sparkline · 7-day sparkline**. Each sparkline has a faint baseline; tiles share a per-tile vertical scale. Dry beds noted (`trocken`).
- **Footer:** `aktualisiert vor 2 min · 4 Knoten · alle OK`.
- Wheel idle on this page (room to add later, e.g. select a tile to drill in).

### Page 3 — Actuators (`AKTOREN · PUMPEN`) — **interactive**
- **Header:** title, `n aktiv` count, page dots, clock.
- **Row per pump** (4 rows, fixed 5-column grid): **Zone** (name truncates + node id) · **State** (1-bit **rounded toggle switch** + EIN/AUS + last-run / running-since) · **Moisture vs target** (current %, fill bar, **target tick** `Ziel 35%` — fill left of tick ⇒ pump triggers) · **Mode** (`AUTO`, trigger reason) · **Timing** (minutes today, daily Ø).
- **Active pump** row is accented (light fill + black left edge) and shows `▶ EIN · läuft …`.
- **Interaction:** rotate wheel → move **focus ring** between rows; press **CONF** → toggle the focused pump. (With mock data, toggling flips local state; later it sends a pump command.)
- **Footer:** `Bewässerung automatisch · Schwelle 35%`.

## 6. Mock data model (for the UI milestone)

- **1 environment station:** air temp, humidity, air pressure (+ tendency), light.
- **4 sensor nodes:** Beet 1, Beet 2, Beet 3, Gewächshaus — each: soil moisture %, soil temp.
- **4 pumps** (1:1 with nodes): on/off, mode, target threshold, last-run, runtime today, daily Ø.
- **History:** ~1 month stored; windows shown = **12 h** (fine) and **7 days** (context). Main chart shows 7-day temp + 7-day Ø soil moisture.
- Representative values (keep consistent across pages): beds 41 / 37 / 52 / 29 %; soil temps 18.2 / 17.8 / 16.9 / 21.4 °C; air 22.4 °C / 58 % / 1013 hPa ↗ / 12k lux; Gewächshaus is dry (29% < 35%) and watering.

## 7. Rendering architecture (decouples UI from hardware)

Goal chosen with the user: **host-renderable** UI so layouts can be previewed/iterated off-device, with the *same* drawing code running on the panel.

- UI draws into an **Adafruit `GFXcanvas1`** (1-bit framebuffer, 792×272 ≈ 27 KB). All page render functions take a `GFX`/canvas reference and the data model — **no hardware calls inside rendering**.
- **On device:** push the canvas buffer to the SSD1683 panel (full or partial refresh) via the panel driver.
- **On host (simulator):** dump the same canvas buffer to a PNG/PGM for preview — trivial because the framebuffer is identical.
- **Open item:** confirm the panel-driver library for this exact 792×272 SSD1683 panel (GxEPD2 vs Elecrow's library/fork). Decide during Mission 1; the canvas-blit approach keeps this isolated.

## 8. Open questions (revisit later)

- Weather glyph source: derived from our own sensors (bright+dry ⇒ sun) vs real forecast (needs internet/gateway). Assume **sensor-derived** for now.
- Time source: ESP32 RTC loses time on power-off; NTP via gateway or an RTC module — defer.
- Whether Page 2 tiles also become drill-in interactive later.

## 9. Missions

- **Mission 1 (next):** PlatformIO project foundation + render the 3 pages with mock data + working navigation (Menu/Exit page switching, wheel focus + CONF toggle on Page 3). Deploy to the CrowPanel.
- **Mission 2 (later):** data structures, time-series storage (SD/LittleFS), ring buffers for 12 h / 7 d / 1 month.
- **Later:** ESP-NOW transport, sensor/pump/gateway nodes, Telegram, Home Assistant/ESPHome migration.
