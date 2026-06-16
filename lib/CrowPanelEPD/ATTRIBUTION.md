# Attribution

This directory contains **vendored third-party code** for driving the Elecrow
CrowPanel ESP32 5.79" e-paper panel (dual SSD1683 controllers, 792×272, 1-bit).

- **Source:** https://github.com/cubic9com/crowpanel-5.79_weather-display (`/src`)
- **License:** MIT — Copyright (c) 2025 cubic9com
- The driver itself derives from Elecrow's official CrowPanel example code.

Vendored files: `spi.{h,cpp}`, `EPD_Init.{h,cpp}`, `EPD.{h,cpp}`, `EPDfont.h`, `ChivoMonoFont.h`.

We use this layer only as the **panel transport**: build a 1-bit framebuffer
(800×272 memory layout, 27200 bytes) and push it via `EPD_Display()` + `EPD_Update()`.
Our own UI rendering (Adafruit GFX into a GFXcanvas1) sits on top — see
`docs/specs/2026-06-16-display-ui-design.md` §7.

Pin mapping (from `spi.h`): SCK=12, MOSI=11, RES=47, DC=46, CS=45, BUSY=48.
