# Mission 2 Design — Local Time-Series Storage (Display Node)

**Status:** Approved (2026-06-16)
**Scope:** Replace the mock `UiModel` on the display node with real data structures and a local, persistent time-series store on the SD card, fed by a built-in simulator. The store is designed behind a Repository interface so it can later become a local cache in front of a cloud server with **no UI changes**.
**Out of scope (deferred):** ESP-NOW / real sensor nodes; cloud sync & CSV/JSON export (interface ready, not built); NTP (swapped behind `now()` later); a dedicated month-view UI page (data is stored, no page yet); `hasAlert`/storm-warning data source (stays a constant).

Design diagrams (orientation, 1-bit-agnostic) live in `.superpowers/brainstorm/31869-1781643989/`:
- `framing.html` — layer stack + now-vs-later evolution
- `ts-model.html` — multi-resolution rings vs single raw log
- `format.html` — binary ring-file layout
- `data-model.html` — stored vs computed split, generic-series shape
- `fast-tier.html` — high-resolution sampling while a pump runs

---

## 1. Hardware & context (drives the design)

| Property | Value |
|---|---|
| MCU | ESP32-S3, 240 MHz, **8 MB flash, 8 MB PSRAM** |
| Storage | **microSD over SPI** — a 64 GB card is available (capacity is a non-issue) |
| Clock | **No RTC**, no network this mission |
| Display | 1-bit e-paper, renders infrequently (reads are rare; writes ~15 min) |

The whole time-series working set is only a few KB, so it lives comfortably in RAM (PSRAM); SD provides durability + long history.

## 2. Architecture

The UI keeps consuming a `UiModel`, but it is **built by a Repository** instead of `mockModel()`. A simulator feeds a time-series store; a soft clock stamps everything; SD persists it.

```
SimSource ──▶ TimeSeriesStore (RAM rings) ⇄ SdStorage (binary files on SD)
                     │  now()                          ▲ persists clock too
                     ▼
              GardenRepository ──▶ builds UiModel ──▶ UI pages (unchanged)
```

**The Repository is the cloud-ready seam.** Today: `LocalRepository` over `TimeSeriesStore`. Later: `CachedCloudRepository` implements the *same* `IGardenRepository` interface; the UI and pages never change.

### Proposed file layout
Mirrors how `Canvas1` is kept pure and host-testable:

- `lib/TimeSeries/` — **pure C++, no Arduino deps** (compiles in `env:host_sim`):
  - `Ring` — fixed-capacity binary ring buffer (header + slots, in-place round-robin)
  - `Series` — one metric: raw ring + optional daily ring + downsample-on-write rollup
  - `TimeSeriesStore` — registry of series keyed by `(nodeId, metric)` + aggregate queries
  - `SeriesStorage` — interface for loading/saving rings; `SdStorage` (device) / null-or-file (host)
- `src/data/` — app glue:
  - `GardenRepository` (`IGardenRepository` + `LocalRepository`)
  - `SimSource` — seeding + live simulation
  - `Clock` — soft clock + `now()`
  - `PumpLog` — pump event log + run aggregation
- `UiModel` stays as the view-model; only its *source* changes.

## 3. Data structures

### 3.1 Records (fixed-size, binary)
- **Raw record:** `{ uint32_t ts; int16_t value; }` = 6 B.
  `value` is fixed-point per series (each `Series` carries a `scale`): soil% ×1, air/soil temp ×10, pressure ×1, humidity ×1, lux ×¼. The ×¼ lux scale keeps even full daylight in range: ~100k lux ÷ 4 = 25 000 < `int16_t` ceiling (32 767), so bright sun cannot silently overflow.
- **Daily record:** `{ uint32_t ts; int16_t avg; }` = 6 B.
- **Ring file header:** `magic · version · recordSize · capacity · head · count`.

A raw point is an **interval aggregate** (the average of whatever samples fell in its window), not necessarily a single instantaneous reading — see §6.

### 3.2 Series set & retention
Each `(nodeId, metric)` is an independent `Series` with its own tier config.

| Series | Tiers | Notes |
|---|---|---|
| soil moisture % (×4 nodes) | raw + daily | sparklines, chart bars, control |
| soil temp °C (×4 nodes) | raw | Detail current value |
| air temp °C (env) | raw + daily | hero, chart line, day lo/hi |
| pressure hPa (env) | raw | current + rising/falling trend |
| humidity % (env) | raw | current |
| light lux (env) | raw | current |

- **Raw ring:** 15-min cadence × 48 h ≈ **192 slots** (~1.2 KB/series).
- **Daily ring:** 1/day × **400 days** (~2.4 KB/series). Month data is stored & available even though no page renders it yet.
- Daily tier exists only where a 7-day / month view needs it.

### 3.3 Round-robin & downsample-on-write
New sample → write at slot `head`, `head = (head + 1) % capacity`, `count` saturates at `capacity`. When a raw sample crosses into a new day, the completed day's average is appended to the daily ring (`day lo/hi` for *today* is computed from raw, which always holds today).

## 4. Store query API
What the repository calls (windows: 12 h reads from the raw ring; 7 d / month from the daily ring):

- `latest(node, metric)` → current value (reflects the freshest fine sample while pumping, §6)
- `sampleWindow(node, metric, window, nPoints)` → N-point sparkline / chart series (e.g. 7)
- `averageAcrossNodes(metric, window, nPoints)` → chart soil-Ø bars
- `minMaxToday(node, metric)` → day lo / hi
- `trend(metric)` → rising / steady / falling (pressure)

## 5. Repository & derived values
`IGardenRepository` exposes `UiModel buildModel()` plus a pump-toggle method. `LocalRepository` implements it over `TimeSeriesStore` + `PumpLog`. **Computed on read, never stored:** soil Ø across nodes, pressure trend, day high/low, "feels like" (temp+humidity), weather glyph (sun/cloud/rain/moon), "dry" flag (soil < threshold), pump minutes-today & avg/day.

`buildModel()` must populate **every** field of the existing `UiModel` (`src/ui/ui_model.h`): `latest()` → current values; `sampleWindow(...,7)` → `ChartSeries` and the per-node `spark12h`/`spark7d` arrays; `averageAcrossNodes` → chart soil bars; `minMaxToday` → `dayLoC`/`dayHiC`; `trend` → `pressureTrend`. **Exception:** `hasAlert` / `alertText` are not data-driven this mission — they stay hardcoded constants (storm-warning source is out of scope). `dateLine`/`clock` come from `now()`.

## 6. High-resolution sampling while a pump runs

Separates **control** (when to stop the pump) from **storage** (what's persisted). One tier finer than raw, transient:

```
FINE  — 30 s, RAM-only, active ONLY while that node's pump is running
        ├─▶ pump control: stop when soil ≥ target%  (reads freshest sample → no overshoot)
        └─▶ averaged over the 15-min window ⤵
RAW   — 15 min, persisted, ~48 h   (point = interval average; 1 sample idle, ~30 while pumping)
        └─▶ averaged over the day ⤵
DAILY — 1/day, persisted, ~400 d
```

- The fine tier is a small RAM-only buffer; it is **not persisted**. `latest()` returns its freshest sample so the pump stops within ~30 s instead of up to 15 min (less overwatering).
- It rolls up into the existing 15-min raw point — **no new persisted tier, no SD-format change**. **Rollup rule:** the raw point is the **average of every sample collected during that 15-min window**, regardless of source — idle 15-min reads and any 30 s fine samples are pooled equally. A pump that starts partway through a window simply contributes more samples to that window's average; partial windows are averaged over whatever samples they actually contain.
- Adaptive cadence: a node bursts to 30 s only while its pump is active (matches real low-power sensor-node behavior later).
- **Deferred (YAGNI):** persisting the 30 s detail for "zoom into a watering event" — add a short-retention fine ring behind the same interface if ever wanted.

## 7. Pumps

Append-only **event log** on SD, with a self-contained audit field set:

```
PumpEvent { uint32_t ts; uint8_t pumpId; uint8_t event /*START|STOP*/;
            int16_t soilPct; int16_t thresholdPct; }
```

- `soilPct` = freshest soil reading for the zone at the event moment (from the fine tier).
- `thresholdPct` = the threshold in effect **at that time** — the dry-threshold on START, the target on STOP. Stored so adherence can be audited *as specified* even after thresholds are later re-tuned in config.
- `minutes-today` and `avg/day` are computed by pairing START/STOP on read. A **dangling START** (pump still running, or a STOP lost to a reboot) is treated as *running until `now()`* for the duration calc, which also keeps the snapshot's active/running state correct. Current on / active / mode / target live in the snapshot.
- The Page-3 CONF toggle writes a pump event via the repository instead of mutating a model copy.

## 8. Clock, persistence & resilience

- **`Clock`:** epoch baseline (first boot from a compile-time constant) advanced by `millis()`; baseline persisted to `/garden/clock.dat` **on every raw write** (~15 min) so a power loss rewinds the clock by at most one raw interval. All time read through `now()`; later swap the source to gateway `MSG_TIME_SYNC` / NTP behind the same call. The 12 h / 7 d / month windows are relative to `now()`, so they work even if absolute time drifts.
- **Write-through:** each sample updates the RAM ring and its SD slot (15-min cadence → trivial I/O).
- **No card / corrupt card:** log a warning and run **RAM-only** (seed in RAM, no persistence); the display still works.
- **SD layout:** `/garden/<node>_<metric>_raw.bin`, `_daily.bin`, `pumps.log`, `clock.dat`.

## 9. Simulator & seeding

- **First boot (no SD data):** seed ~30 daily points + ~48 h of raw per series with plausible curves (diurnal temp, slow soil decline + watering bumps) and a few past pump runs; reproduce the locked scenario (greenhouse dry & watering). Mark as seeded so it isn't redone.
- **Running:** a ~15-min timer generates the next sample (random-walk + diurnal model) and appends; soil < threshold triggers a pump START, soil ≥ target triggers STOP (both logged with audit fields). While a pump is active, the affected node samples at 30 s (fine tier).
- **Debug:** an accelerated-tick flag compresses time so data visibly moves on the bench in seconds.

## 10. Testing & integration

- The core (`lib/TimeSeries`, repository math, simulator model) is pure C++ → **TDD in the existing `env:host_sim` native env**: ring wrap, downsample rollup, window sampling, cross-node average, trend, fine-tier rollup, pump-run pairing.
- `host_preview.cpp` renders the **real** pages from the simulated store → iterate on the Mac, no flashing.
- `display_controller_app.cpp` swaps `mockModel()` → `repository.buildModel()`; rewire the Page-3 toggle to emit a pump event.

## 11. Locale & conventions
German labels, DD.MM.YYYY, 24 h, metric (unchanged). Specs in `docs/specs/`, plans in `docs/plans/`, log in `docs/memory.md`.
