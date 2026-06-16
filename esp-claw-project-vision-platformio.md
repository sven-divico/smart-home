# ESP-Claw Garden Automation Project

## 1. Project Idea and Vision

The ESP-Claw project is a modular ESP32-based garden monitoring and automation system. The goal is to build a reliable, extensible and locally understandable sensor-and-actuator network that can monitor soil and environmental conditions, display the current system state inside the house, and control at least one watering actuator such as a water pump.

The project should start small but be designed so it can scale to multiple garden zones, additional sensor types and more advanced automation logic. In addition the project serves as hands-on teaching on esp-now, SPI Bus Communications, Data Series storage.

### Roadmap
Initial phase focuses on setting up the underlying mechanics, second phase shall migrate to a scalable version that leverages Homeassistant and esphome, in addition esp-claw shall be veluated

### Core goals

- Measure soil moisture and environmental data across different garden areas.
- Use low-power ESP32 sensor nodes where possible.
- Use ESP-NOW for direct local communication between ESP32 devices.
- Add at least one actuator node for pump or valve control.
- Use one centrally located indoor e-paper display for local visibility.
- Use an ESP-Claw gateway for Telegram integration and possibly future Home Assistant or MQTT integration.
- Keep the architecture modular so hardware can be exchanged without rewriting the whole system.
- Organize firmware development cleanly in PlatformIO with one repository and multiple build environments.

### Guiding principles

- Start with a minimal working version before adding complexity.
- Separate roles clearly: sensing, control, display, gateway and actuation.
- Keep communication protocol code shared and versioned.
- Avoid hard-coding secrets into the repository.
- Prefer reusable drivers and shared libraries over copy-pasted firmware.
- Design the system so additional nodes can be added later with predictable effort.

---

## 2. Initial Architecture Idea

The system is organized around several logical roles. Each role may run on a different ESP32 board, but the firmware should remain part of one shared PlatformIO project.

### Main components

| Component | Role | Main responsibility |
|---|---|---|
| Main controller | Central coordinator | Receives measurements, stores local state, coordinates watering decisions |
| Sensor nodes | Distributed measurement nodes | Measure soil moisture, temperature, battery voltage and possibly light/wind |
| Pump node | Actuator node | Controls pump or valve and reports actuator status |
| Display controller | Indoor display node | Shows latest values, warnings and watering state on an e-paper display |
| ESP-Claw gateway | Communication gateway | Handles Telegram, Wi-Fi, optional MQTT/Home Assistant bridge |
| Optional Home Assistant server | External dashboard/automation | Future integration for dashboards, history and automations |

### Proposed communication model

The first version should use ESP-NOW as the local communication backbone between nodes.

```text
Sensor Nodes  --->  Main Controller  --->  Display Controller
       \              |
        \             v
         ----------> Pump Node

Main Controller / Gateway  <-->  ESP-Claw Gateway  <-->  Telegram / Wi-Fi / MQTT
```

Depending on implementation details, the ESP-Claw gateway can either be:

1. A separate ESP32/ESP32-S3 device connected to the main controller via serial or ESP-NOW.
2. Combined with the main controller if the selected board has enough memory, stable Wi-Fi and enough spare GPIOs.

For the first version, a separate gateway is cleaner because Telegram/Wi-Fi logic is isolated from the core sensor/control logic.

### Milestones

Idea: build skills and complexity step by step, understanding of basics is more important than speed

Warm-up:

The warm up aims to setup a maintainable project structure, using platformio
1. get the display node up and running (ELECROW ESP32 E-Ink Display 5.79 Inch, https://www.elecrow.com/download/product/DIS08792E/User_Manual_for_ESP32_E-Paper_HMI_Display.pdf)
2. create an initial UI based on locally stored mock-data of measurements of the past month
   1. temperature, light, humidity, air pressure
   2. 4 sensor points, each having: soil humidity, temperature

Navigation on Display shall have at least three pages, navigation via rotary switch:
1. main page - Date and time, icon for current weather (sun, moon, clowd, rain) overview of current status plus graph of time series on last 7 days
2. detail page - 5 tiles, 
   1. 1st tile, 1st row, full span - temperature, light, humidity, air pressure
   2. 2nd - 5th tile, 2nd row, all tiles equal size - one per node: soil humidity, temperature
3. actuator page: showing status of 4 water pumps, plus actual soil humidity at the point



Version 0.1 should include:

1. One sensor node.
2. One main controller.
3. One display controller.
4. One pump/actuator node.
5. One ESP-Claw gateway for Telegram status messages.

Minimum successful flow:

```text
Sensor node reads soil moisture
        ↓
Sensor node sends ESP-NOW packet
        ↓
Main controller receives and validates packet
        ↓
Display controller shows the current value
        ↓
Pump node can receive a manual ON/OFF command
        ↓
Gateway can send a Telegram status update
```

---

## 3. Device Roles and Naming

Use a clear naming convention from the beginning.

| Device | Suggested name | Node ID range |
|---|---|---:|
| Main controller | `ctrl-main-01` | 1 |
| ESP-Claw gateway | `gw-espclaw-01` | 2 |
| Display controller | `display-epaper-01` | 3 |
| Sensor node bed 1 | `sensor-bed-01` | 101 |
| Sensor node bed 2 | `sensor-bed-02` | 102 |
| Sensor node greenhouse | `sensor-greenhouse-01` | 110 |
| Pump node | `pump-main-01` | 201 |

Recommended node ID structure:

| Range | Purpose |
|---:|---|
| 1-9 | Core infrastructure nodes |
| 100-199 | Sensor nodes |
| 200-299 | Actuator nodes |
| 300-399 | Display or UI nodes |
| 900-999 | Development/test nodes |

---

## 4. Proposed Repository Structure

The recommended structure is one Git repository and one PlatformIO project with multiple build environments.

```text
esp-claw/
├── platformio.ini
├── include/
│   ├── project_config.h
│   ├── node_roles.h
│   ├── secrets.example.h
│   └── secrets.h              # local only, not committed
├── src/
│   ├── main.cpp
│   ├── app_select.cpp
│   ├── app_select.h
│   └── apps/
│       ├── main_controller_app.cpp
│       ├── main_controller_app.h
│       ├── display_controller_app.cpp
│       ├── display_controller_app.h
│       ├── esp_claw_gateway_app.cpp
│       ├── esp_claw_gateway_app.h
│       ├── sensor_node_app.cpp
│       ├── sensor_node_app.h
│       ├── pump_node_app.cpp
│       └── pump_node_app.h
├── lib/
│   ├── EspClawProtocol/
│   │   ├── src/
│   │   └── include/
│   ├── SensorDrivers/
│   │   ├── src/
│   │   └── include/
│   ├── DisplayDrivers/
│   │   ├── src/
│   │   └── include/
│   └── DeviceConfig/
│       ├── src/
│       └── include/
├── boards/
│   └── waveshare_esp32s3_32mb_16mbpsram.json
├── data/
│   └── config-template.json
├── test/
└── README.md
```

### Main design idea

The firmware should be selected by PlatformIO build flags.

`src/main.cpp` should remain minimal:

```cpp
#include <Arduino.h>
#include "app_select.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  app_setup();
}

void loop() {
  app_loop();
}
```

`src/app_select.cpp` selects the correct app at compile time:

```cpp
#include "app_select.h"

#if defined(ROLE_MAIN_CONTROLLER)
  #include "apps/main_controller_app.h"
#elif defined(ROLE_DISPLAY_CONTROLLER)
  #include "apps/display_controller_app.h"
#elif defined(ROLE_ESP_CLAW_GATEWAY)
  #include "apps/esp_claw_gateway_app.h"
#elif defined(ROLE_SENSOR_NODE)
  #include "apps/sensor_node_app.h"
#elif defined(ROLE_PUMP_NODE)
  #include "apps/pump_node_app.h"
#else
  #error "No device role defined"
#endif
```

---

## 5. Shared Libraries

Reusable code should go into `lib/`.

### `lib/EspClawProtocol`

Responsible for shared communication structures, message types, packet validation and serialization.

Example message types:

```cpp
enum MessageType : uint8_t {
  MSG_SENSOR_READING = 1,
  MSG_PUMP_COMMAND = 2,
  MSG_PUMP_STATUS = 3,
  MSG_HEARTBEAT = 4,
  MSG_TIME_SYNC = 5
};
```

Example packet header:

```cpp
struct PacketHeader {
  uint8_t protocolVersion;
  uint8_t messageType;
  uint16_t payloadLength;
  uint32_t senderNodeId;
  uint32_t sequenceNumber;
};
```

Recommended:

```cpp
#define ESP_CLAW_PROTOCOL_VERSION 1
```

### `lib/SensorDrivers`

Responsible for sensor-specific abstractions.

Possible drivers:

- Capacitive soil moisture sensor.
- Temperature sensor.
- Battery voltage reader.
- Light sensor.
- Wind or rain sensor later.

### `lib/DisplayDrivers`

Responsible for display rendering and page layout.

The display controller should not decide watering logic. It should receive system state and render it.

### `lib/DeviceConfig`

Responsible for shared constants, pin mappings, node IDs and default calibration values.

---

## 6. Configuration Strategy

Use three configuration layers.

### Level 1: Build-time role

Defined in `platformio.ini`.

Examples:

```ini
-D ROLE_SENSOR_NODE
-D USE_ESPNOW
-D USE_DEEP_SLEEP
```

This controls which code is compiled.

### Level 2: Device-specific compile-time constants

Useful for early prototypes.

Examples:

```ini
-D NODE_ID=101
-D DEVICE_NAME="sensor-bed-01"
-D SOIL_SENSOR_PIN=34
-D BATTERY_PIN=35
```

### Level 3: Runtime configuration

For later scaling, store configuration in flash using LittleFS/SPIFFS.

Example:

```json
{
  "nodeId": 101,
  "deviceName": "sensor-bed-01",
  "moistureDry": 2890,
  "moistureWet": 1320,
  "sendIntervalSeconds": 900
}
```

For version 0.1, compile-time constants are simpler and recommended.

---

## 7. Secrets Handling

Do not commit Wi-Fi passwords, Telegram tokens or MQTT credentials.

Use:

```text
include/secrets.example.h
include/secrets.h
```

Commit only `secrets.example.h`.

Example:

```cpp
#pragma once

#define WIFI_SSID "your-wifi"
#define WIFI_PASSWORD "your-password"
#define TELEGRAM_BOT_TOKEN "your-token"
#define TELEGRAM_CHAT_ID "your-chat-id"
```

Add this to `.gitignore`:

```gitignore
include/secrets.h
.pio/
.vscode/.browse.c_cpp.db*
.vscode/c_cpp_properties.json
```

Only the gateway firmware should include real secrets.

---

## 8. Proposed PlatformIO Configuration

Initial `platformio.ini`:

```ini
[platformio]
default_envs = sensor_node

[env]
platform = espressif32
framework = arduino
monitor_speed = 115200
upload_speed = 921600
build_flags =
  -D CORE_DEBUG_LEVEL=3

lib_deps =
  bblanchon/ArduinoJson
  adafruit/Adafruit BusIO

; --------------------------------------------------
; Main controller
; --------------------------------------------------

[env:main_controller]
extends = env
board = esp32-s3-devkitc-1
build_flags =
  ${env.build_flags}
  -D ROLE_MAIN_CONTROLLER
  -D DEVICE_NAME="ctrl-main-01"
  -D NODE_ID=1
  -D USE_ESPNOW
  -D USE_WIFI_OPTIONAL

; --------------------------------------------------
; Display controller
; --------------------------------------------------

[env:display_controller]
extends = env
board = esp32-s3-devkitc-1
build_flags =
  ${env.build_flags}
  -D ROLE_DISPLAY_CONTROLLER
  -D DEVICE_NAME="display-epaper-01"
  -D NODE_ID=3
  -D USE_DISPLAY
  -D USE_ESPNOW

lib_deps =
  ${env.lib_deps}
  zinggjm/GxEPD2

; --------------------------------------------------
; ESP-Claw gateway
; --------------------------------------------------

[env:esp_claw_gateway]
extends = env
board = esp32-s3-devkitc-1
build_flags =
  ${env.build_flags}
  -D ROLE_ESP_CLAW_GATEWAY
  -D DEVICE_NAME="gw-espclaw-01"
  -D NODE_ID=2
  -D USE_WIFI
  -D USE_TELEGRAM
  -D USE_ESPNOW

lib_deps =
  ${env.lib_deps}
  knolleary/PubSubClient

; --------------------------------------------------
; Generic sensor node firmware
; --------------------------------------------------

[env:sensor_node]
extends = env
board = esp32dev
build_flags =
  ${env.build_flags}
  -D ROLE_SENSOR_NODE
  -D DEVICE_NAME="sensor-node-generic"
  -D NODE_ID=100
  -D USE_ESPNOW
  -D USE_DEEP_SLEEP

; --------------------------------------------------
; Sensor bed 1
; --------------------------------------------------

[env:sensor_bed_01]
extends = sensor_node
build_flags =
  ${env:sensor_node.build_flags}
  -D DEVICE_NAME="sensor-bed-01"
  -D NODE_ID=101
  -D SOIL_SENSOR_PIN=34
  -D BATTERY_PIN=35

; --------------------------------------------------
; Sensor bed 2
; --------------------------------------------------

[env:sensor_bed_02]
extends = sensor_node
build_flags =
  ${env:sensor_node.build_flags}
  -D DEVICE_NAME="sensor-bed-02"
  -D NODE_ID=102
  -D SOIL_SENSOR_PIN=34
  -D BATTERY_PIN=35

; --------------------------------------------------
; Pump / actuator node
; --------------------------------------------------

[env:pump_node]
extends = env
board = esp32dev
build_flags =
  ${env.build_flags}
  -D ROLE_PUMP_NODE
  -D DEVICE_NAME="pump-main-01"
  -D NODE_ID=201
  -D USE_ESPNOW
  -D USE_PUMP_RELAY
  -D PUMP_RELAY_PIN=26
```

---

## 9. Build and Upload Workflow

Build default environment:

```bash
pio run
```

Build one specific environment:

```bash
pio run -e sensor_bed_01
```

Upload to a connected board:

```bash
pio run -e sensor_bed_01 -t upload
```

Open serial monitor:

```bash
pio device monitor -e sensor_bed_01
```

Upload gateway firmware:

```bash
pio run -e esp_claw_gateway -t upload
pio device monitor -e esp_claw_gateway
```

Upload display firmware:

```bash
pio run -e display_controller -t upload
pio device monitor -e display_controller
```

---

## 10. Recommended Development Sequence

### Phase 1: PlatformIO foundation

- Create repository.
- Add base `platformio.ini`.
- Add the app selection structure.
- Verify that each environment builds.

### Phase 2: ESP-NOW protocol

- Create `EspClawProtocol`.
- Send a test packet from one sensor node to the main controller.
- Add sequence number and protocol version.

### Phase 3: Sensor node

- Read soil moisture.
- Add averaging.
- Add battery voltage reading.
- Send periodic ESP-NOW packets.
- Add deep sleep later.

### Phase 4: Main controller

- Receive packets.
- Validate sender and protocol version.
- Store last known sensor values.
- Print status over serial.
- Forward state to display/gateway later.

### Phase 5: Display controller

- Receive or request latest state.
- Render simple e-paper screen.
- Add pages later if needed.

### Phase 6: Pump node

- Receive pump command.
- Switch pump relay/MOSFET.
- Add hard safety timeout.
- Report pump status.

### Phase 7: ESP-Claw gateway

- Connect to Wi-Fi.
- Add Telegram status command.
- Optional later: MQTT or Home Assistant bridge.

---

## 11. Important Design Decisions

### One repository instead of many

Use one repository because the project has shared protocol, shared drivers and shared configuration. Multiple repositories would create avoidable duplication.

### Multiple PlatformIO environments

Use one PlatformIO environment per firmware variant or hardware variant.

Examples:

- `main_controller`
- `display_controller`
- `esp_claw_gateway`
- `sensor_node`
- `sensor_bed_01`
- `sensor_bed_02`
- `pump_node`

### Role-specific apps

Each role has its own app file, but all apps share the same protocol and driver libraries.

### Compile-time node identity at first

For early prototypes, define `NODE_ID`, `DEVICE_NAME` and pin mappings in `platformio.ini`.

Move to runtime config only once the hardware is stable.

### Separate gateway preferred

A separate ESP-Claw gateway is cleaner at the beginning because Telegram/Wi-Fi/MQTT logic is isolated from the core control loop.

---

## 12. Open Questions

These points should be clarified during hardware selection and first prototyping:

- Which exact ESP32 board will be used for the main controller?
- Which exact ESP32-S3 board will be used for the ESP-Claw gateway?
- Will the display controller use a raw e-paper panel or an integrated ESP32 e-paper board?
- Should the main controller and gateway communicate via ESP-NOW, serial or both?
- How many sensor nodes are required for version 0.1?
- Should pump control be direct relay-based or via a safer dedicated pump driver module?
- Should Home Assistant integration be included in v1 or deferred until the core ESP-NOW system works?

---

## 13. Recommended Next Step

Create the PlatformIO project and implement the empty app skeleton first.

The first technical success criterion should be:

```text
Each PlatformIO environment builds successfully and prints its role, node ID and device name over serial.
```

Only after that should ESP-NOW, sensors, display and pump logic be added.
