#pragma once
#include <stdint.h>

#pragma pack(push, 1)
// One time-series point. 6 bytes on disk and in RAM.
struct Sample { uint32_t ts; int16_t value; };

// One pump event. Self-contained audit: soil + threshold at the moment.
struct PumpEvent {
  uint32_t ts;
  uint8_t  pumpId;
  uint8_t  event;        // EV_START / EV_STOP
  int16_t  soilPct;      // freshest soil reading at the event
  int16_t  thresholdPct; // dry-threshold on START, target on STOP
};
#pragma pack(pop)

enum PumpEventType : uint8_t { EV_START = 0, EV_STOP = 1 };
