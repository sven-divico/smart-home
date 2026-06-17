#pragma once
#include <stdint.h>
#include "Sample.h"
#include "metrics.h"

// Append-only ring of pump events. Durations are derived by pairing START->STOP;
// a dangling START (still running, or STOP lost to reboot) counts until `now`.
class PumpLog {
public:
  static const uint16_t CAP = 256;

  void append(const PumpEvent& e);
  uint16_t size() const { return ring_.size(); }
  const PumpEvent& at(uint16_t i) const { return ring_.at(i); }

  int  minutesToday(uint8_t pumpId, uint32_t now) const;
  int  avgPerDay(uint8_t pumpId, uint32_t now, int days) const;
  bool isRunning(uint8_t pumpId, uint32_t now) const;

private:
  // Sum run-seconds for pumpId within [from, now]; dangling START runs to now.
  uint32_t runSeconds(uint8_t pumpId, uint32_t from, uint32_t now) const;

  PumpEvent buf_[CAP];
  // Reuse Ring's index math via a tiny inline ring of PumpEvent.
  struct EvtRing {
    PumpEvent* b; uint16_t cap, head = 0, count = 0;
    void push(const PumpEvent& e){ b[head]=e; head=(head+1)%cap; if(count<cap)count++; }
    uint16_t size() const { return count<cap?count:cap; }
    const PumpEvent& at(uint16_t i) const {
      uint16_t s=(head+cap-size())%cap; return b[(s+i)%cap];
    }
  } ring_{buf_, CAP};
};
