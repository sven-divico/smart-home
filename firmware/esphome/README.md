# ESPHome track — node firmware for Home Assistant

This folder holds the **ESPHome** nodes of ESP-Claw (the commodity sensor / actuator /
display track). See [`docs/architecture.md`](../../docs/architecture.md) for the
dual-track rule and the full node registry.

```
firmware/esphome/
├── common/base.yaml     # shared boilerplate every node includes (wifi/api/ota/logger/captive_portal)
├── soil-pump-01.yaml    # reference template — copy this for a new node
├── amoled-panel-01.yaml # Waveshare AMOLED touch panel (Phase-1 bring-up)
├── secrets.yaml         # (gitignored) shared wifi_ssid / wifi_password
└── README.md
```

## Adding a node (the convention)

Copy `soil-pump-01.yaml` and keep it small — shared bits come from the package:

```yaml
substitutions:
  node_name: my-node-01
  friendly: "My Node"
esphome:
  name: ${node_name}
  friendly_name: ${friendly}
esp32:
  board: <your board>           # board stays per-node (they differ)
  framework: { type: esp-idf }
packages:
  base: !include common/base.yaml   # ← wifi/api/ota/logger/captive_portal
# ...only this node's own sensors/switches/display below
```

`common/base.yaml` reads `${node_name}` (used for the fallback-AP SSID), so every node
gets a `<node> setup` recovery hotspot for free. Secrets live once in `secrets.yaml`.

Then add a row to the registry in `docs/architecture.md`.

## Flashing & logging

**First flash = USB.** After that, **flash and log over the network (OTA)** — no USB:

```bash
esphome run  <node>.yaml --device <ip>     # OTA update
esphome logs <node>.yaml --device <ip>     # live logs over the network
```

For the **first** USB flash, this repo's ESP32-S3 boards need esptool's bundled python and
the merged factory image. The reliable recipe (learned on the Feather — see gotchas):

```bash
ESPY=$(head -1 "$(command -v esphome)" | sed 's/^#!//')
BUILD=.esphome/build/<node>/.pioenvs/<node>
esphome compile <node>.yaml
"$ESPY" -m esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX \
  --before default-reset --after watchdog-reset --baud 115200 \
  write-flash -z --flash-size detect 0x0 $BUILD/firmware.factory.bin
```

### ESP32-S3 native-USB gotchas (Feather-discovered, likely apply to other S3 boards)
1. **Use `--after watchdog-reset`, not `hard-reset`.** esptool's hard-reset can leave the
   chip in DOWNLOAD mode (`boot:0x0`), so the app never starts (no LED, no WiFi).
2. **115200 baud** for USB flashing (460800 gave `Serial data stream stopped`).
3. **Reading USB serial: open the port passively** — never assert DTR/RTS (RTS strap-resets
   into download mode). Easier: just use OTA logs once it's on WiFi.
4. **`framework: esp-idf`** (Arduino gave `'USBSerial' was not declared` and no USB logs).

## Nodes

### `soil-pump-01` — ✅ live in HA
Adafruit Feather ESP32-S3 (8MB). Soil moisture sensor + pump relay. IP `192.168.178.192`.
Open TODOs: confirm a clean power-on boots the app (not download mode); calibrate moisture %
(`calibrate_linear` in the yaml); wire + test pump (need relay-module photo for polarity).

### `amoled-panel-01` — ✅ Phase 2 (LVGL dashboard, live)
Waveshare ESP32-S3-Touch-AMOLED-1.75**C**. IP `192.168.178.193`, MAC `44:1b:f6:85:53:84`.
LVGL dashboard: a moisture ring + % for Plant 1 (pulled from HA via the `homeassistant`
sensor platform), an "HA connected" status line, and a touch button that toggles
`switch.plant_1_plant_1_pump` in HA. Both directions confirmed working. Config follows the
[official ESPHome device page](https://devices.esphome.io/devices/waveshare-esp32-s3-touch-amoled-175/).

Gotchas that cost us time (all resolved):
- **Adoption + service-call permission.** The `homeassistant` sensor bindings only get values
  once the panel is added in HA. The pump button additionally needs the device's
  **"Allow the device to make Home Assistant service calls"** checkbox enabled (it resets to off
  when you delete+re-add the device) — without it the button does nothing while everything else works.
- **Doubled entity ids.** Real ids are `sensor.plant_1_plant_1_soil_moisture` / `switch.plant_1_plant_1_pump`
  (NOT single `plant_1`) because soil-pump-01 sets device name "Plant 1" AND entity name "Plant 1 …",
  and HA prepends the device name. The panel's `moisture_entity` / `pump_entity` substitutions point
  at the doubled ids. (Cleanup: drop the `${friendly}` prefix from soil-pump-01's entity names.)
- **`esphome upload` does NOT recompile** — it pushes the last-built binary. After editing the yaml,
  run `esphome compile` (or `esphome run`) before/instead of `upload`, or you flash stale firmware.

**Touch driver:** `cst9217` is **vendored in-tree** under `components/cst9217/` (from
shelson/esphome-cst9217 @ 126c017, MIT) and patched for the ESPHome 2026.6 removal of
`status_set_error(const char*)`. We own the source via `external_components: { type: local }`.
Drop it if/when cst9217 lands in mainline ESPHome.

Notes from bring-up:
- **Flash is 32MB on the 1.75C** (wiki says 16MB for non-C). We declare `flash_size: 16MB`
  anyway — 16MB+OTA stays on stable partitions; 32MB+OTA needs experimental features. Plenty of room.
- **Touch `cst9217`** is an external component (`github://shelson/esphome-cst9217`), source-reviewed
  and **pinned to commit `126c017`**. Compiling pulls third-party code — re-review before bumping the ref.
- Harmless boot-time `Invalid ACK 0x08 vs 0xAB` warning from the touch driver; touches parse fine after.
- Native USB-Serial/JTAG → same `watchdog-reset` first-flash recipe as the Feather; updates now via OTA.
