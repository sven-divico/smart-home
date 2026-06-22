# ESPHome node: `soil-pump-01` (Plant 1)

Fast-track node to get **one capacitive soil sensor** + **one pump relay** into Home Assistant.
Intentionally simple / throwaway; we re-architect later. Config: [`soil-pump-01.yaml`](soil-pump-01.yaml).

## Status (2026-06-18)

✅ **Working and adopted in Home Assistant.**

- Board: **Adafruit Feather ESP32-S3, 8MB flash, no PSRAM** (`adafruit_feather_esp32s3_nopsram`)
- Framework: **ESP-IDF** (switched from Arduino — see gotchas)
- WiFi: joins **`d-42`**, signal ~-49 dB, **IP `192.168.178.192`**, hostname `soil-pump-01.local`
- HA API: port **6053**, no encryption. Device + entities visible in HA.
- MAC: `b4:3a:45:34:e7:a4`

### Entities in HA
- `Plant 1 Soil Voltage` (V) — raw ADC
- `Plant 1 Soil Moisture` (%) — derived; **NOT yet calibrated** (placeholder values)
- `Plant 1 Pump` (switch) — relay; 30s safety auto-off
- `Plant 1 Status` (light) — onboard red LED via `status_led`

## Toolchain

ESPHome CLI installed on the Mac via Homebrew (`brew install esphome`, v2026.5.3).
HA on the Pi is **HA OS** (the "Apps" menu = the old Add-on Store); the ESPHome "App"
is also installed there for later. We chose the Mac CLI so config stays in this repo.

`secrets.yaml` (gitignored) holds `wifi_ssid` / `wifi_password`.

## Flashing procedure (hard-won — follow exactly)

This board's **native USB fights esptool**. The reliable recipe:

```bash
cd firmware/esphome
ESPY=$(head -1 "$(command -v esphome)" | sed 's/^#!//')   # esphome's bundled python
BUILD=.esphome/build/soil-pump-01/.pioenvs/soil-pump-01

# 1. compile
esphome compile soil-pump-01.yaml

# 2. flash the MERGED factory image at 0x0, at 115200 baud,
#    and reset with WATCHDOG (NOT hard-reset) so it boots the app.
"$ESPY" -m esptool --chip esp32s3 --port /dev/cu.usbmodem2101 \
  --before default-reset --after watchdog-reset --baud 115200 \
  write-flash -z --flash-size detect 0x0 $BUILD/firmware.factory.bin
```

**Once it's on WiFi, stop using USB — flash & log over the network (OTA):**
```bash
esphome run  soil-pump-01.yaml --device 192.168.178.192   # OTA update
esphome logs soil-pump-01.yaml --device 192.168.178.192   # live logs over network
```

## Gotchas we hit (so we don't repeat them)

1. **Arduino framework wouldn't compile** (`'USBSerial' was not declared`). Fixed by
   switching to `framework: esp-idf`. (Arduino also gave no USB logs.)
2. **`clamp(x, 0.0, 100.0)`** failed — needs `0.0f, 100.0f` (float vs double). Fixed.
3. **esptool `hard-reset` (and physical reset) boots the chip into DOWNLOAD mode**
   (`boot:0x0 (DOWNLOAD)`) instead of the app — the app never starts: no LED, no WiFi.
   Use **`--after watchdog-reset`** to boot the app. A clean VBUS power-on *should* use
   normal strapping and boot the app — **still needs confirming** (see TODO).
4. **Reading USB serial: open the port PASSIVELY** — do NOT assert DTR/RTS. Asserting
   RTS strap-resets the chip into download mode, so you read nothing. (pyserial:
   set `dtr=False; rts=False` before `open()`.) Easier: just use OTA logs now.
5. **`status_led` is OFF when healthy/connected**, ON/blinking only on a problem.
   "LED off" briefly fooled us into thinking the firmware was dead — it was fine.
6. Higher baud (460800) gave `Serial data stream stopped` — use **115200**.

## TODO (next session)

- [ ] **Confirm a clean power-on boots the app** (unplug ~3s, replug; device should
      reappear online in HA within ~30s). If it boots to download instead, investigate
      GPIO0 strapping / hold.
- [ ] **Calibrate moisture %**: read `Plant 1 Soil Voltage` with sensor (a) dry in air,
      (b) tip in water; put those two voltages into `calibrate_linear` in the YAML; re-flash via OTA.
- [ ] **Wire + test the pump.** OPEN QUESTION: the relay's motor-side pins were labeled
      `Low / Level / Trigger` (not standard COM/NO/NC) — need a photo of the relay module
      to confirm terminals + trigger polarity before driving the pump. Pump must be on its
      OWN power supply, switched by the relay — never powered from the Feather.
- [ ] Soil sensor powered from **3.3V** (not 5V) so AOUT stays in ADC range. Signal → A5 (GPIO8).

## Wiring map (Feather silkscreen → module)

| From | To | Note |
|------|----|------|
| soil VCC | `3V` | 3.3V keeps AOUT in ADC range |
| soil GND | `GND` | |
| soil AOUT | `A5` (GPIO8) | ADC1, WiFi-safe |
| relay VCC | `USB` (5V) | coil power |
| relay GND | `GND` | |
| relay IN | `D6` (GPIO6) | control, active-low (`inverted: true`) |
| relay COM/NO | pump + separate supply | isolated from Feather |
