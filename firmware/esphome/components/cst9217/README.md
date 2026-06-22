# cst9217 (vendored, patched)

CST9217 capacitive-touch driver for the Waveshare ESP32-S3-Touch-AMOLED-1.75C,
used by `amoled-panel-01.yaml`.

## Provenance

Vendored from [shelson/esphome-cst9217](https://github.com/shelson/esphome-cst9217)
at commit `126c0174a17857a7496284ab9d06ba5d7a141fbd` (MIT, © 2025 Simon Helson —
see `LICENSE`). Previously pulled as a pinned `external_components` git source; copied
in-tree so we own the patch below and no longer depend on an upstream that may go stale.

## Why it's patched

Upstream calls `status_set_error(str_sprintf(...).c_str())`. The `const char*`
overload of `Component::status_set_error()` is deprecated in ESPHome 2026.5 and
**removed in 2026.6** — building against 2026.6+ would fail. The status flag now
needs a static `LogString` (`LOG_STR("...")`), which cannot carry a runtime-formatted
string.

### Change (in `cst9217_touchscreen.cpp`, chip-ID-mismatch path)

```cpp
// before (upstream):
this->status_set_error(str_sprintf("CST9217 Chip ID mismatch, expected 0x%04X, got 0x%04X",
                                   CST9217_CHIP_ID, this->chip_id_).c_str());

// after:
ESP_LOGE(TAG, "CST9217 Chip ID mismatch, expected 0x%04X, got 0x%04X", CST9217_CHIP_ID, this->chip_id_);
this->status_set_error(LOG_STR("CST9217 Chip ID mismatch"));
```

The dynamic "expected vs got" detail moves to an `ESP_LOGE` line so nothing is lost.

The two `status_set_warning(...c_str())` calls in `update_touches()` are left as-is:
the `const char*` overload of `status_set_warning()` is **not** deprecated.

## Upstreaming / future cleanup

cst9217 is **not** in mainline ESPHome as of 2026.5.3 (only `cst226` / `cst816`
ship). If it lands upstream, drop this whole directory and the `external_components`
block in `amoled-panel-01.yaml`.
