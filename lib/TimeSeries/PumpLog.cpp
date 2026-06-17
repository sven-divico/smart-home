#include "PumpLog.h"
using namespace ts;

void PumpLog::append(const PumpEvent& e) { ring_.push(e); }

uint32_t PumpLog::runSeconds(uint8_t pumpId, uint32_t from, uint32_t now) const {
  uint32_t total = 0;
  bool open = false; uint32_t startTs = 0;
  for (uint16_t i = 0; i < ring_.size(); i++) {
    const PumpEvent& e = ring_.at(i);
    if (e.pumpId != pumpId) continue;
    // Consecutive STARTs (e.g. a duplicate after a reboot) take the latest ts;
    // the earlier un-stopped run is intentionally dropped rather than guessed at.
    if (e.event == EV_START) { open = true; startTs = e.ts; }
    else if (e.event == EV_STOP && open) {
      uint32_t a = startTs < from ? from : startTs;
      if (e.ts > a) total += e.ts - a;
      open = false;
    }
  }
  if (open) {  // dangling START -> runs until now
    uint32_t a = startTs < from ? from : startTs;
    if (now > a) total += now - a;
  }
  return total;
}

int PumpLog::minutesToday(uint8_t pumpId, uint32_t now) const {
  uint32_t dayStart = (now / DAY_S) * DAY_S;
  return (int)(runSeconds(pumpId, dayStart, now) / 60);
}

int PumpLog::avgPerDay(uint8_t pumpId, uint32_t now, int days) const {
  if (days <= 0) return 0;
  uint32_t from = (now > (uint32_t)days * DAY_S) ? now - days * DAY_S : 0;
  return (int)((runSeconds(pumpId, from, now) / 60) / days);
}

bool PumpLog::isRunning(uint8_t pumpId, uint32_t now) const {
  bool open = false;
  for (uint16_t i = 0; i < ring_.size(); i++) {
    const PumpEvent& e = ring_.at(i);
    if (e.pumpId != pumpId) continue;
    open = (e.event == EV_START);
  }
  (void)now;
  return open;
}
