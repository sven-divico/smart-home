# Garden IoT — Communications Technical Specification

| | |
|---|---|
| **Spec ID** | `garden-comms` |
| **Protocol version** | `0x01` (v0.1, draft) |
| **Status** | Draft for implementation planning |
| **Date** | 2026-06-17 |


> **Purpose.** This document defines the *wire protocol and payload formats* for communication between field nodes and the central gateway. It is the contract an implementation must satisfy. It deliberately does **not** prescribe the implementation plan, code structure, or hardware BOM — those come next. Everything here is byte-precise so that node and gateway firmware can be developed independently and still interoperate.

---

## 1. System overview & roles

```
   Home Assistant (Raspberry Pi)
            ▲
            │  ESPHome native API (WiFi)       
            ▼
   ┌─────────────────┐    ESP-NOW (unicast, encrypted)   ┌──────────────┐
   │     GATEWAY      │◄────────────────────────────────►│   NODE  #n   │
   │ mains, always on │   this spec defines this link    │ battery+solar│
   │ WiFi + ESP-NOW   │◄────────────────────────────────►│   NODE  #m   │
   └─────────────────┘                                   └──────────────┘
```

| Role | Count | Power | Responsibility |
|------|-------|-------|----------------|
| **Gateway** | 1 (per cell) | Mains, always on | Bridge ESP-NOW ↔ Home Assistant. Holds each node's *desired state*, command sequencing, availability tracking, key store, node registry. |
| **Node** | N | Battery + solar, deep-sleep | Sample local sensors, report telemetry, optionally drive **one** actuator. Polls gateway on wake; reconciles actuator to desired state. |

**Scope of this spec:** the ESP-NOW link between gateway and nodes — frame format, message types, payloads, timing, sequencing, security, and the actuator desired-state/lease semantics. The gateway↔Home-Assistant link (ESPHome API / entities) is referenced only where it shapes the protocol.

---

## 2. Node capability model

A node is **not** single-purpose. One node hosts **multiple sensors** (close-coupled, typically on a shared **SPI** bus, optionally I²C) and **at most one actuator** (e.g. a valve or pump). This consolidates ESP32s: one enclosure per garden location covers all the sensing there plus, optionally, the local irrigation outlet.

| Property | Rule |
|----------|------|
| Sensors per node | 0..N (bounded by frame size, §5.4 — ≈36 readings/frame) |
| Actuators per node | 0 or 1 |
| Local sensor bus | SPI primary (short runs, same enclosure); I²C/ADC/1-Wire permitted |
| Sampling | All local sensors sampled in one wake, before transmit |
| Self-description | Node announces its sensor set + actuator presence via `HELLO` (§6.1) so the gateway/HA can auto-provision entities |

**Power behaviour follows function (see §9):**

- **Pure sensor node** (no actuator): deep-sleep, wake on long interval, report, sleep.
- **Actuator-capable node, actuator OFF:** deep-sleep, wake on poll interval, report + poll, sleep. *(This is the "actuator sleeping if off" behaviour.)*
- **Actuator-capable node, actuator ON (pumping/watering):** stays awake, holds the output, polls on the short active interval, runs the local deadman. *(This is "on if pump is operated.")*

A node that carries an actuator therefore **decouples its poll cadence from its report cadence** (§14): it must wake often enough to be a responsive, fail-safe actuator, but it need not transmit a full sensor payload every time.

---

## 3. Transport layer (ESP-NOW)

| Parameter | Value / rule |
|-----------|--------------|
| Protocol | ESP-NOW over 2.4 GHz, station mode on nodes |
| **Channel** | Fixed, equal to the gateway's WiFi channel. Provisioned constant `RF_CHANNEL`. Router must be pinned to this channel. |
| Addressing | 6-byte MAC. Node knows `GATEWAY_MAC` (provisioned). Gateway adds each node as an encrypted peer (from registry or on first `HELLO`). |
| Cast type | **Unicast** in both directions (gives MAC-layer delivery ACK; broadcast does not). |
| Encryption | ESP-NOW link-layer encryption: 16-byte **PMK** + per-peer 16-byte **LMK** (§12). All frames encrypted. |
| Max app payload | Treat as **200 bytes usable** (ESP-NOW v1 limit 250 B incl. overhead; stay conservative). No fragmentation in v0.1. |
| Delivery feedback | ESP-NOW send-callback (delivered / not delivered) used for retry logic (§11). |

**Channel acquisition (node):** node uses `RF_CHANNEL` directly in v0.1. Optional future: scan for `GATEWAY_MAC`'s channel on boot.

---

## 4. Protocol overview (exchanges)

All exchanges are **node-initiated** (nodes are the sleepy side; the gateway never pushes to a sleeping radio). The gateway answers within the node's RX window.

### 4.1 Boot / announce
```
NODE                                  GATEWAY
  │ ── HELLO (caps, fw, actuator?) ──► │  register/refresh node, (re)create HA entities
  │ ◄──────── STATE (ack, cfg) ─────── │  may push initial config + next_poll
```

### 4.2 Normal wake — combined report + poll (the core loop)
```
NODE  (wakes, samples all sensors)     GATEWAY
  │ ── REPORT (readings + status) ───► │  ingest readings → HA; dedup by seq
  │                                    │  look up node's desired actuator state
  │ ◄── STATE (ack + desired + next) ─ │  ack seq; if actuator-capable: desired_mode,
  │                                    │  remaining_s, cmd_id; next_poll_s
  │ (reconcile actuator; sleep or hold)│
```
One round trip does both jobs: telemetry up, desired-state/lease down. Pure sensor nodes simply ignore the actuator fields.

### 4.3 Active watering (actuator ON)
Same `REPORT`/`STATE` exchange, repeated on the **active** interval. Each `STATE` with `desired_mode=ON` **renews the lease** (re-arms the deadman). Missed exchanges → fail-closed (§10).

---

## 5. Frame format

Every frame = **8-byte common header** + type-specific payload. All multi-byte integers are **little-endian**.

### 5.1 Common header (8 bytes)

| Off | Size | Field | Notes |
|----:|-----:|-------|-------|
| 0 | 1 | `magic` | `0x47` ('G'). Drop frame if mismatch. |
| 1 | 1 | `ver` | Protocol version. `0x01`. Major mismatch ⇒ drop (§12.4). |
| 2 | 1 | `type` | Message type (§6 enum). |
| 3 | 1 | `flags` | Bitfield (§5.2). |
| 4 | 2 | `node_id` | `uint16` logical node id (1..65534; 0 = unassigned, 0xFFFF = broadcast/reserved). |
| 6 | 2 | `seq` | `uint16` per-node monotonic sequence, wraps. Increments on every node TX. |

### 5.2 `flags` bitfield

| Bit | Name | Meaning |
|----:|------|---------|
| 0 | `ACK_REQ` | Sender expects a `STATE`/`ACK` response within RX window. |
| 1 | `BOOT` | First frame since reset; gateway resets its `seq` expectation for this node. |
| 2 | `ACTUATOR_CAP` | Node carries an actuator → gateway must return desired-state fields in `STATE`. |
| 3 | `WATERING` | Actuator is currently ON (node is in active/hold state). |
| 4 | `LOW_BATT` | Battery below node-local threshold (advisory). |
| 5 | `CONFIG_REQ` | Node requests a `CONFIG` push. |
| 6–7 | reserved | Must be 0. |

### 5.3 Reading TLV (used in payloads)

A single sensor/telemetry reading is a fixed **5-byte** record:

| Off | Size | Field | Notes |
|----:|-----:|-------|-------|
| 0 | 1 | `quantity` | Quantity id (§8.1). |
| 1 | 4 | `value` | `int32` scaled per the quantity's scale factor (§8.1). |

### 5.4 Size budget

Header 8 B + count fields ≤ 8 B leaves ~184 B ⇒ **≈36 readings per frame**. More than enough for one location. Nodes exceeding this must reduce per-wake readings (decimation, §14); v0.1 has **no fragmentation**.

---

## 6. Message types

| `type` | Name | Direction | Purpose |
|-------:|------|-----------|---------|
| `0x01` | `HELLO` | node → gw | Announce identity + capabilities on boot. |
| `0x02` | `REPORT` | node → gw | Telemetry (+ implicit poll if `ACTUATOR_CAP`). |
| `0x03` | `STATE` | gw → node | Ack + desired actuator state + next-poll directive. |
| `0x04` | `ACK` | gw → node | Bare acknowledgement (no actuator payload). |
| `0x05` | `NACK` | gw → node | Rejection / error (§8.4). |
| `0x06` | `CONFIG` | gw → node | Push operating parameters (§17). |

### 6.1 `HELLO` payload (node → gw)

| Off | Size | Field | Notes |
|----:|-----:|-------|-------|
| 0 | 1 | `fw_major` | Firmware version. |
| 1 | 1 | `fw_minor` | |
| 2 | 1 | `fw_patch` | |
| 3 | 1 | `hw_rev` | Board/hardware revision. |
| 4 | 1 | `actuator_type` | §8.2 (`0x00` = none). |
| 5 | 1 | `sensor_count` | Number of quantity ids that follow. |
| 6 | `sensor_count` | `quantity[]` | One `uint8` quantity id per sensor the node provides (§8.1). |

Gateway response: `STATE` (with `ack_seq` = HELLO `seq`, optional initial `CONFIG` may follow or be folded in).

### 6.2 `REPORT` payload (node → gw)

| Off | Size | Field | Notes |
|----:|-----:|-------|-------|
| 0 | 4 | `uptime_s` | `uint32` seconds since boot (relative clock; gateway stamps wall time). |
| 4 | 1 | `reading_count` | Number of 5-byte readings that follow. |
| 5 | 5×`n` | `readings[]` | Reading TLVs (§5.3). Includes sensors **and** telemetry: battery (`0x06`), and—if actuator present—current actuator state (`0x40`) and remaining seconds (`0x41`). |

If `flags.ACTUATOR_CAP` is set, the gateway MUST answer with `STATE` carrying desired-state fields; otherwise it answers with `ACK` (or `STATE` with `has_actuator=0`).

### 6.3 `STATE` payload (gw → node)

| Off | Size | Field | Notes |
|----:|-----:|-------|-------|
| 0 | 2 | `ack_seq` | `uint16` node `seq` being acknowledged. |
| 2 | 1 | `result` | `0x00` = OK; else error code (§8.4). |
| 3 | 1 | `has_actuator` | `1` if actuator command fields are present & valid. |
| 4 | 2 | `cmd_id` | `uint16` monotonic command id (replay guard, §11.3). |
| 6 | 1 | `desired_mode` | `0` = OFF/CLOSED, `1` = ON/OPEN. |
| 7 | 2 | `remaining_s` | `uint16` seconds the actuator should remain ON from receipt. `0` if OFF. |
| 9 | 2 | `next_poll_s` | `uint16` seconds until node's next scheduled wake (gateway-driven cadence). |
| 11 | 1 | `flags2` | reserved (0) for future (e.g. force-config). |

If `has_actuator = 0`, the actuator fields at offsets 4–8 (`cmd_id`, `desired_mode`, `remaining_s`) are ignored by the node; `next_poll_s` (offset 9) still applies.

### 6.4 `ACK` payload (gw → node)

| Off | Size | Field |
|----:|-----:|-------|
| 0 | 2 | `ack_seq` |
| 2 | 1 | `result` |
| 3 | 2 | `next_poll_s` |

### 6.5 `NACK` payload (gw → node)

| Off | Size | Field |
|----:|-----:|-------|
| 0 | 2 | `ack_seq` |
| 2 | 1 | `error` (§8.4) |

### 6.6 `CONFIG` payload (gw → node)

Pushes operating parameters; all `uint16` seconds unless noted. Sent in response to `CONFIG_REQ`, on `HELLO`, or when the gateway changes policy.

| Off | Size | Field | Notes |
|----:|-----:|-------|-------|
| 0 | 2 | `poll_idle_s` | Idle wake interval. |
| 2 | 2 | `poll_active_s` | Wake interval while watering. |
| 4 | 2 | `rx_window_ms` | RX listen window (ms). |
| 6 | 2 | `deadman_max_s` | Absolute actuator runtime cap (node-enforced, §10). |
| 8 | 2 | `min_off_s` | Minimum off-time between runs. |
| 10 | 1 | `report_decimation` | Send full sensor payload every Nth poll (§14). |
| 11 | 1 | `missed_poll_limit` | Consecutive missed active polls → fail-closed (§10). |

---

## 8. Data dictionaries

### 8.1 Quantity ids & scaling

`value_engineering = value_int32 / scale`. Choose `int32` for uniform parsing; range is ample.

| id | Quantity | Unit | Scale | Example (`value`→eng) |
|----:|----------|------|------:|-----------------------|
| `0x01` | soil_moisture | % | 100 | 4150 → 41.50 % |
| `0x02` | soil_temperature | °C | 100 | 1830 → 18.30 °C |
| `0x03` | air_temperature | °C | 100 | 2170 → 21.70 °C |
| `0x04` | air_humidity | % | 100 | 5800 → 58.00 % |
| `0x05` | illuminance | lx | 1 | 12000 → 12000 lx |
| `0x06` | battery_voltage | V | 1000 | 3920 → 3.920 V |
| `0x07` | soil_conductivity | µS/cm | 1 | 1500 → 1500 |
| `0x08` | pressure | hPa | 100 | 101325 → 1013.25 |
| `0x09` | rain | mm | 100 | 250 → 2.50 mm |
| `0x0A` | flow_rate | L/min | 100 | 650 → 6.50 |
| `0x0B` | tank_level | % | 100 | 7250 → 72.50 % |
| `0x0C`–`0x3F` | reserved (sensors) | | | |
| `0x40` | actuator_state | enum | 1 | 0 = off/closed, 1 = on/open |
| `0x41` | actuator_remaining_s | s | 1 | seconds left on current run |
| `0x42`–`0x4F` | reserved (actuator telemetry) | | | |

> Quantity ids are stable across the fleet. Adding a sensor type = allocate a new id here; never reuse.

### 8.2 Actuator types

| id | Type | Fail-safe default |
|----:|------|-------------------|
| `0x00` | none | — |
| `0x01` | valve_nc (normally-closed solenoid) | de-energized = closed |
| `0x02` | pump | de-energized = off |
| `0x03` | valve_nc + pump (combined outlet) | both de-energized |
| `0x04`–`0xFF` | reserved | |

> **Latching/bistable valves are not permitted** (`0x01`–`0x03` are all fail-closed-on-power-loss types). See §10 and brainstorm §2B.

### 8.3 Node id allocation

`node_id` is a `uint16` assigned in the **node registry** (§13). `0x0000` = unassigned (pre-provision), `0xFFFF` = reserved. Gateway maps `node_id ↔ MAC ↔ friendly name/zone`.

### 8.4 Result / error codes

| code | Meaning |
|----:|---------|
| `0x00` | OK |
| `0x01` | UNKNOWN_NODE (not in registry) |
| `0x02` | BAD_VERSION |
| `0x03` | BAD_SEQ (stale/replayed) |
| `0x04` | BAD_CMD_ID (stale actuator command) |
| `0x05` | MALFORMED |
| `0x06` | NOT_AUTHORIZED |
| `0x07` | RATE_LIMITED (min_off / caps) |

---

## 9. Polling & timing model

Cadence is **gateway-directed** via `next_poll_s` in every `STATE`/`ACK`, bounded by node-local clamps. This lets the gateway move a node between idle and active cadence centrally, while the node retains independent safety limits.

| Constant | Default | Meaning |
|----------|--------:|---------|
| `POLL_IDLE` | 30 s | Wake interval, actuator OFF (or pure sensor node may use a longer value, e.g. 300–1800 s). |
| `POLL_ACTIVE` | 10 s | Wake/poll interval while watering. |
| `RX_WINDOW` | 250 ms | Listen window after TX (gateway answers in single-digit ms; 250 ms covers one retry). |
| `N_RETRY` | 3 | Send attempts before giving up this wake. |
| `RETRY_BACKOFF` | 50 ms + jitter | Between retries. |
| `JITTER` | ±10 % | Applied to all poll intervals to avoid fleet synchronization. |
| `AVAIL_TIMEOUT` | 3 × node interval | Gateway marks node unavailable in HA. |

**Latency:** worst-case command-to-action latency ≈ `POLL_IDLE` (acceptable for irrigation). Early-stop responsiveness while watering ≈ `POLL_ACTIVE`.

---

## 10. Actuator desired-state & fail-safe semantics

This section is normative and ties directly to brainstorm §2A (layers) and §2B (polling).

### 10.1 Gateway-held desired state
Per actuator-capable node the gateway stores:
```
desired = { mode: OFF | ON, deadline_wall_ts, cmd_id }
```
On each `STATE`, the gateway computes `remaining_s = max(0, deadline_wall_ts − now)` and sends `desired_mode`, `remaining_s`, `cmd_id`.

### 10.2 Node reconciliation (on valid `STATE`, `has_actuator=1`)
1. Verify `cmd_id` is not stale (§11.3). If stale → reject (`BAD_CMD_ID` behaviour), do **not** change actuator.
2. If `desired_mode = ON`:
   - `lease = min(remaining_s, deadman_max_s)`
   - energize actuator (if not already), arm/re-arm local **deadman timer** to `lease`
   - record `t_last_state = now`
   - enter/stay in **HOLD**; schedule next wake at `next_poll_s`; **do not deep-sleep**
3. If `desired_mode = OFF`: de-energize; return to sleepy idle loop.

### 10.3 Fail-closed triggers (any one closes the actuator)
| # | Trigger | Mechanism |
|--:|---------|-----------|
| 1 | Local **deadman** expiry | `min(remaining_s, deadman_max_s)` elapsed since last valid ON `STATE`. |
| 2 | **Missed polls** | `(now − t_last_state) > missed_poll_limit × POLL_ACTIVE` ⇒ no permission ⇒ close. |
| 3 | Explicit `desired_mode = OFF` | Gateway command. |
| 4 | **Power loss / reset / crash** | Hardware: NC valve / de-energized pump (Layer 0). |
| 5 | **Local caps** | Run exceeds `deadman_max_s`, or `min_off_s` not satisfied ⇒ refuse/stop. |

**Invariant:** *to start or keep an actuator ON, the node must keep receiving valid ON `STATE`s. Absence of communication always converges to OFF.* Opening requires comms; closing happens in their absence.

### 10.4 Caps are node-enforced
`deadman_max_s` and `min_off_s` are enforced **on the node**, independent of gateway/HA. A command requesting `remaining_s > deadman_max_s` is clamped, not honored in full.

---

## 11. Reliability & sequencing

### 11.1 Delivery
Unicast ESP-NOW yields a send-callback (delivered/failed). On `failed` or no `STATE` within `RX_WINDOW`, retry up to `N_RETRY` with `RETRY_BACKOFF`+jitter.

- **Sensor node, exhausted retries:** abandon this wake, sleep; fresh readings go next cycle. No buffering required in v0.1 (telemetry is soft-state).
- **Actuator node, exhausted retries while ON:** treat as missed poll → counts toward `missed_poll_limit` → fail-closed.

### 11.2 `seq` (node TX sequence)
`uint16`, increments every node transmission, wraps. Gateway **dedups** by `(node_id, seq)` within a sliding window (e.g. last 64). `BOOT` flag tells the gateway to reset its expectation. Wrap handled by windowed comparison (treat `a` newer than `b` if `(uint16)(a−b) < 0x8000`).

### 11.3 `cmd_id` (gateway command sequence, replay guard)
`uint16`, gateway increments **when the desired actuation changes**; held constant while a lease is merely being renewed (so renewals share a `cmd_id`).
Node rule: accept `STATE` whose `cmd_id` is **equal to** the last accepted (renewal) **or newer** (`(uint16)(cmd_id − last) < 0x8000`, and ≠ a past value). **Reject older** `cmd_id` (stale replay) → no actuator change. Persist `last_cmd_id` across deep sleep (RTC memory).

### 11.4 Idempotency
Because the gateway sends **desired state** (not deltas), a lost or duplicated `STATE` is self-correcting: the next successful exchange re-converges the actuator. No command is ever "lost."

---

## 12. Security

### 12.1 Confidentiality & integrity
All ESP-NOW peers are **encrypted** (AES-128-CCM at the link layer). Keys:
- `PMK` — one 16-byte primary master key for the fleet (provisioned to gateway + all nodes).
- `LMK` — one 16-byte per-peer key (gateway holds one per node; node holds the gateway's).

### 12.2 Replay protection
- Telemetry: `seq` dedup (low stakes).
- **Actuation:** `cmd_id` monotonic guard (§11.3) + link-layer encryption. A captured ON frame cannot be replayed to re-open later (its `cmd_id` is stale and `remaining_s` would already be expired).

### 12.3 Key provisioning
Keys are compile-time/`secrets`-provisioned in v0.1 (not over-the-air). A node with the wrong key cannot join (frames fail to decrypt). Document key rotation as a future item.

### 12.4 Versioning
`ver` byte. **Major** version must match between node and gateway; on mismatch the receiver replies `NACK(BAD_VERSION)` (gateway) or drops (node). Within a major version, unknown `quantity`/`type` ids must be **ignored**, not fatal (forward compatibility).

---

## 13. Node registry (gateway-side, config)

The gateway owns the authoritative registry. Suggested schema (YAML/JSON; format finalized in implementation):

```yaml
nodes:
  - node_id: 1
    name: bed-east
    mac: "24:6F:28:AA:BB:01"
    zone: bed-east
    actuator: { type: valve_nc, deadman_max_s: 1800, min_off_s: 120 }
    sensors: [soil_moisture, soil_temperature, air_temperature, air_humidity, battery_voltage]
  - node_id: 2
    name: greenhouse
    mac: "24:6F:28:AA:BB:02"
    zone: greenhouse
    actuator: null
    sensors: [air_temperature, air_humidity, illuminance, battery_voltage]
```

### 13.1 Home Assistant entity mapping
| Source | HA entity |
|--------|-----------|
| `node_id`+`quantity` | `sensor.<name>_<quantity>` (e.g. `sensor.bed_east_soil_moisture`) |
| actuator | `switch.<name>_valve` (or `valve.` domain) + `sensor.<name>_run_remaining` |
| availability | `binary_sensor.<name>_online` driven by `AVAIL_TIMEOUT` |
| battery | `sensor.<name>_battery_voltage` + low-battery automation |

The gateway re-exposes received values as its own ESPHome entities (or via MQTT) so HA sees normal devices. (Mechanism = gateway↔HA, out of scope here.)

---

## 14. Multi-sensor sampling & report decimation

Because an actuator-capable node must poll frequently but its sensors change slowly:

- **Poll cadence** is driven by actuator needs (`POLL_IDLE`/`POLL_ACTIVE`).
- **Report cadence** is decoupled: include the full `readings[]` payload only every `report_decimation`-th poll, **or** when any reading crosses a per-quantity change threshold, **or** on the first poll after boot. Intermediate polls may carry a minimal `REPORT` (`reading_count` small — e.g. just battery + actuator state) purely to exercise the poll/lease.
- **While watering:** the node is awake anyway, so it MAY report full sensor data every active poll at no extra energy cost.
- **Sampling:** all local sensors are read in a single pre-transmit sampling pass each wake that reports; SPI sensors share the bus sequentially.

Pure sensor nodes set `report_decimation = 1` and use a long `POLL_IDLE`.

---

## 15. State machines

### 15.1 Node lifecycle
```
        power on / reset
              │  (BOOT flag)
              ▼
          ┌───────┐  HELLO ack ┌────────┐
          │ HELLO │───────────►│  IDLE  │◄───────────────┐
          └───────┘            │(sleep) │                │
                               └───┬────┘                │
                       wake timer  │                     │ desired=OFF
                                   ▼                     │ or deadman/missed
                              ┌─────────┐ REPORT/STATE    │
                              │ SAMPLE  │────────────►┌───┴────┐
                              │  + TX   │  desired=ON  │  HOLD  │
                              └─────────┘─────────────►│(awake, │
                                   │ desired=OFF/no act│ valve  │
                                   ▼                   │  ON,   │
                              back to IDLE (sleep)     │ poll @ │
                                                       │ ACTIVE)│
                                                       └────────┘
```

### 15.2 Actuator (within node)
```
 OFF ──(valid STATE: ON, cmd_id ok)──► ON[deadman=min(remaining,max)]
  ▲                                         │
  │  deadman expiry │ missed_poll_limit │   │ each valid ON STATE → re-arm deadman
  │  STATE:OFF │ caps │ reset(NC)           │
  └─────────────────────────────────────────┘
```

---

## 16. Error handling & edge cases

| Case | Behaviour |
|------|-----------|
| No `STATE` within `RX_WINDOW` | Retry ×`N_RETRY`; then sleep (sensor) / count missed poll (actuator). |
| `seq` duplicate at gateway | Ignore payload, still re-send last `STATE`/`ACK` (idempotent). |
| Stale `cmd_id` at node | Reject actuation change; keep current safe behaviour. |
| Node misses many polls then wakes | Reconcile to current desired state on next success; nothing buffered. |
| Node reset mid-watering | Hardware closes (NC); reboot → `HELLO` → re-reads desired; may re-open if still wanted. |
| Unknown `quantity`/`type` (newer node) | Ignore that field; do not fault. |
| Wrong key / channel | Frames don't decrypt/arrive; node treated as offline after `AVAIL_TIMEOUT`. |
| `remaining_s` > `deadman_max_s` | Clamp to cap on node. |
| Gateway down / HA reboot | Nodes get no `STATE` → all actuators fail-closed within ≤ `missed_poll_limit × POLL_ACTIVE`. |

---

## 17. Configuration constants (summary)

| Name | Default | Owner | Notes |
|------|--------:|-------|-------|
| `RF_CHANNEL` | (site) | both | = router WiFi channel |
| `GATEWAY_MAC` | (site) | node | provisioned |
| `PMK` / `LMK` | (secret) | both | 16 B each |
| `POLL_IDLE` | 30 s | gw→node | sensor-only nodes may use 300–1800 s |
| `POLL_ACTIVE` | 10 s | gw→node | while watering |
| `RX_WINDOW` | 250 ms | node | |
| `N_RETRY` | 3 | node | |
| `DEADMAN_MAX` | 1800 s | node | hard cap |
| `MIN_OFF` | 120 s | node | between runs |
| `REPORT_DECIMATION` | 1 | gw→node | full readings every Nth poll |
| `MISSED_POLL_LIMIT` | 3 | gw→node | → fail-closed |
| `AVAIL_TIMEOUT` | 3× interval | gw | HA availability |

---

## 18. Decisions made & open questions

**Decisions baked into this spec (revisit if needed):**
1. **Custom application protocol over ESP-NOW** (not ESPHome `packet_transport`), because the sleepy-node poll = request/response with desired-state + lease, which a push-only transport can't serve on-demand. `packet_transport` remains a fallback for always-on push-only telemetry.
2. **Combined `REPORT`+`STATE` round trip** — telemetry up and desired-state down in one exchange.
3. **Desired-state (idempotent), not imperative commands** — robust to loss.
4. **Gateway-directed cadence** via `next_poll_s`; node-enforced safety caps.
5. **`int32` scaled readings, 5 B each** — uniform parsing over byte-packing micro-optimization.
6. **NC/fail-closed actuators only**; latching valves disallowed.

**Open (resolve in implementation planning):**
- ESPHome realisation of the custom protocol: raw `espnow` send/`on_receive` lambdas vs a small external component vs ESP-IDF. (Validate the `espnow` component's raw API surface.)
- Gateway→HA exposure: native ESPHome API entities vs MQTT bridge.
- Exact SPI sensor set per node + per-quantity change thresholds for decimation.
- Key rotation / OTA provisioning strategy (deferred from v0.1).
- Whether to add a `uint8` payload CRC beyond ESP-NOW's link integrity (likely unnecessary).
- RTC-memory layout for persisted `last_cmd_id`, `seq`, lease state across deep sleep.

---

## 19. Glossary

| Term | Meaning |
|------|---------|
| **Node** | Battery+solar ESP32 with ≥1 sensor and ≤1 actuator. |
| **Gateway** | Always-on ESP32 bridging ESP-NOW ↔ Home Assistant. |
| **Deadman** | Node-local timer that closes the actuator when it expires. |
| **Lease** | The right to keep an actuator ON, granted per `STATE` and renewed each poll. |
| **Desired state** | Gateway-held target (`OFF`/`ON`+deadline) the node reconciles to. |
| **Decimation** | Sending full sensor payload only every Nth poll. |
| **PMK/LMK** | ESP-NOW primary / local (per-peer) 16-byte encryption keys. |
