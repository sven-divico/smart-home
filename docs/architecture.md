# ESP-Claw — System Architecture & Node Registry

This is the practical "where does a new node go?" reference. It reflects how the
project actually works today, which has evolved from the original ESP-NOW plan in
[`esp-claw-project-vision-platformio.md`](../esp-claw-project-vision-platformio.md)
toward a **Home Assistant–centric** system (the vision doc's anticipated "phase 2").

## Two firmware tracks (dual-track, on purpose)

We deliberately run two firmware paradigms. Pick the track per node by what the node needs:

| Track | Use it when | Lives in | Talks to HA via |
|---|---|---|---|
| **ESPHome** | Commodity sensor / actuator / display nodes — anything HA can drive directly. Default choice. | `firmware/esphome/<node>.yaml` | Native ESPHome API (auto-discovered) |
| **PlatformIO / Arduino** | Bespoke firmware that ESPHome can't express well — rich custom UI, ESP-NOW experiments, low-level control. | repo root (`platformio.ini`, `src/`, `lib/`, build env per node) | (its own logic; HA bridge optional) |

**Rule of thumb:** reach for ESPHome first. Drop to PlatformIO only when a node needs
behavior ESPHome doesn't cover (e.g., the custom e-paper UI with on-device time-series).

### Why the repo looks asymmetric
The PlatformIO project sits at the **repo root** (it predates the split and supports
multiple Arduino nodes via build environments). ESPHome nodes live under
`firmware/esphome/`. This asymmetry is intentional and documented rather than "fixed" by
moving the working e-paper build. If it ever becomes painful, the symmetric option is to
move the PlatformIO project into `firmware/display-epaper-01/`.

## Node registry

ESPHome nodes are identified by **hostname / mDNS** (`<name>.local`), not the numeric
`NODE_ID`s from the ESP-NOW-era vision doc (those only matter for PlatformIO/ESP-NOW nodes).

| Node (hostname) | Track | Board | Role | Status | File |
|---|---|---|---|---|---|
| `display-epaper-01` | PlatformIO | Elecrow CrowPanel 5.79" e-paper (ESP32-S3) | Indoor e-paper display, on-device time-series | In development | `platformio.ini` env `display_controller` |
| `soil-pump-01` | ESPHome | Adafruit Feather ESP32-S3 (8MB) | Soil moisture sensor + pump relay ("Plant 1") | ✅ Live in HA | [`firmware/esphome/soil-pump-01.yaml`](../firmware/esphome/soil-pump-01.yaml) |
| `amoled-panel-01` | ESPHome | Waveshare ESP32-S3-Touch-AMOLED-1.75C | HA touch control panel ("Garden Panel") | ✅ Phase 1 (display+touch+WiFi) @ 192.168.178.193 | [`firmware/esphome/amoled-panel-01.yaml`](../firmware/esphome/amoled-panel-01.yaml) |

## Adding a node

- **ESPHome node:** copy `soil-pump-01.yaml` as a template. Set `node_name` / `friendly`
  substitutions, the `esp32:` board block, `packages: { base: !include common/base.yaml }`
  for the shared wifi/api/ota/logger boilerplate, then add only the node's own hardware.
  Flash + log over the network once it's joined WiFi. Details:
  [`firmware/esphome/README.md`](../firmware/esphome/README.md).
- **PlatformIO node:** add a build env in `platformio.ini` with its `-D ROLE_*` /
  `-D DEVICE_NAME` / board, and a matching app under `src/apps/`.

Then add a row to the registry above.
