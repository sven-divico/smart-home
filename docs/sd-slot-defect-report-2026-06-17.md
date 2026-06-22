# Fault Report — microSD slot non-functional

**Date:** 2026-06-17
**Product:** Elecrow CrowPanel ESP32 5.79" E-Paper HMI Display (model **DIS08792E**, ESP32-S3)

## Summary
The on-board **microSD card slot is non-functional**. A known-good, correctly formatted (FAT32) microSD card is **never recognised** by the board — the SD card fails its low-level SPI initialisation handshake. All other board functions work normally (e-paper display, buttons/rotary input, ESP32-S3, USB/serial, power). The fault is isolated to the SD card interface and is consistent with a **hardware defect on the SD slot** (most likely a bad solder joint / open connection on the SD data line, MISO/DAT0).

## The card and its format are good (defect is on the board)
- The same microSD card **mounts and reads/writes perfectly on a computer**, formatted **FAT32** (verified on macOS — mounts as `DOS_FAT_32`).
- The card and format are therefore confirmed working; only the board's slot fails.

## Software configuration is correct (per Elecrow's own documentation)
The firmware uses exactly the SD wiring published in Elecrow's wiki and example code for this board:

| SD signal | GPIO |
|---|---|
| CS  | IO10 |
| SCK | IO39 |
| MOSI | IO40 |
| MISO | IO13 |

- microSD driven on a dedicated **HSPI** `SPIClass` instance (as in Elecrow's example).
- Board peripheral power rail (GPIO 7) enabled **before** initialising the SD card.
- Internal pull-ups enabled on the SD lines (MISO/MOSI/CS) to assist initialisation.
- Standard Arduino `SD`/`SPI` libraries (ESP32 core).

## Observed failure (serial output at boot)
With the card inserted, SD initialisation fails every time:

```
=== ESP-Claw :: Display Controller ===
Power: enabling peripheral rail (GPIO 7)
[W][sd_diskio.cpp:174] sdCommand(): no token received
[W][sd_diskio.cpp:174] sdCommand(): no token received
[W][sd_diskio.cpp:174] sdCommand(): no token received
[W][sd_diskio.cpp:174] sdCommand(): no token received
[E][sd_diskio.cpp:199] sdCommand(): Card Failed! cmd: 0x37   (CMD55)
[E][sd_diskio.cpp:199] sdCommand(): Card Failed! cmd: 0x29   (ACMD41)
[W][sd_diskio.cpp:547] ff_sd_initialize(): APP_OP_COND failed: 255
[E][sd_diskio.cpp:805] sdcard_mount(): f_mount failed: (3) The physical drive cannot work
```

The card never completes the SPI power-on negotiation (`ACMD41`). Notably, the card's first command (`CMD0`) only succeeds when an **internal** MISO pull-up is forced in software — on a healthy board the slot's own pull-up should make this unnecessary. This strongly indicates the board's **MISO/DAT0 line is not correctly connected** (weak/open pull-up or a defective solder joint), which lets the very first byte through but corrupts all subsequent responses.

## Troubleshooting already performed (to rule out everything but the hardware)
1. ✅ Confirmed the microSD card works and is FAT32-formatted (mounts on a computer).
2. ✅ Re-seated the card multiple times (push-push slot, fully clicked in).
3. ✅ Verified SD pin assignment against the Elecrow wiki and example code (CS10/SCK39/MOSI40/MISO13).
4. ✅ Used a dedicated HSPI SPI instance, as in Elecrow's reference example.
5. ✅ Enabled the peripheral power rail (GPIO 7) before SD initialisation.
6. ✅ Added internal pull-ups on MISO, MOSI and CS.
7. ✅ Tried a reduced SPI clock for initialisation.
8. ❌ In every case the card fails at the same point (`ACMD41` / "no token received").

## Conclusion / request
The microSD slot on this unit is defective (SD interface / MISO line). The rest of the board is fully functional. **Requesting a replacement unit or repair under warranty.**

---
*Prepared for a warranty claim. Technical contact / project: ESP-Claw garden monitoring (CrowPanel display node).*
