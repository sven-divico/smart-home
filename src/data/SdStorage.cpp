#include "SdStorage.h"
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <string.h>
using namespace ts;

// CrowPanel 5.79" microSD is on its OWN SPI bus (separate from the bit-banged
// e-paper on 12/11). Pins confirmed against the Elecrow wiki + example (2026-06-17).
static const int SD_CS   = 10;
static const int SD_SCK  = 39;
static const int SD_MOSI = 40;
static const int SD_MISO = 13;
static const uint16_t MAGIC = 0xC1A0;

// Elecrow's example drives the SD on a dedicated HSPI instance (the global `SPI`
// is FSPI on the S3). Using the global object here fails card init ("no token").
static SPIClass sdSPI(HSPI);

#pragma pack(push,1)
struct RingHeader { uint16_t magic; uint8_t version; uint8_t recordSize;
                    uint16_t capacity; uint16_t head; uint16_t count; };
#pragma pack(pop)

bool SdStorage::begin() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI)) { ok_ = false; return false; }
  SD.mkdir("/garden");
  ok_ = true;
  return true;
}

void SdStorage::pathFor(NodeId n, Metric m, bool daily, char* out, int len) const {
  snprintf(out, len, "/garden/%u_%u_%s.bin", (unsigned)n, (unsigned)m, daily ? "day" : "raw");
}

void SdStorage::appendSample(NodeId n, Metric m, bool daily, const Sample& s) {
  if (!ok_) return;
  char path[48]; pathFor(n, m, daily, path, sizeof(path));
  uint16_t cap = daily ? DAILY_CAP : RAW_CAP;
  File f = SD.open(path, FILE_WRITE);          // opens R/W, created if absent
  if (!f) return;
  RingHeader h;
  if (f.size() >= (int)sizeof(h)) { f.seek(0); f.read((uint8_t*)&h, sizeof(h)); }
  else { h = {MAGIC, 1, (uint8_t)sizeof(Sample), cap, 0, 0}; }
  uint32_t slotOff = sizeof(h) + (uint32_t)h.head * sizeof(Sample);
  if (f.seek(slotOff) && f.write((const uint8_t*)&s, sizeof(Sample)) == sizeof(Sample)) {
    h.head = (h.head + 1) % cap;
    if (h.count < cap) h.count++;
    f.seek(0); f.write((const uint8_t*)&h, sizeof(h));
  }
  f.close();
}

int SdStorage::loadRing(NodeId n, Metric m, bool daily, Sample* out, int maxOut) {
  if (!ok_) return 0;
  char path[48]; pathFor(n, m, daily, path, sizeof(path));
  File f = SD.open(path, FILE_READ);
  if (!f) return 0;
  RingHeader h;
  if (f.size() < (int)sizeof(h)) { f.close(); return 0; }
  f.read((uint8_t*)&h, sizeof(h));
  if (h.magic != MAGIC) { f.close(); return 0; }
  if (h.capacity == 0) { f.close(); return 0; }              // guard div-by-zero on a truncated file
  if (h.recordSize != sizeof(Sample)) { f.close(); return 0; } // layout mismatch -> ignore file
  uint16_t size = h.count < h.capacity ? h.count : h.capacity;
  uint16_t start = (h.head + h.capacity - size) % h.capacity;
  int k = 0;
  for (uint16_t i = 0; i < size && k < maxOut; i++, k++) {
    uint16_t slot = (start + i) % h.capacity;
    f.seek(sizeof(h) + (uint32_t)slot * sizeof(Sample));
    f.read((uint8_t*)&out[k], sizeof(Sample));
  }
  f.close();
  return k;
}

void SdStorage::appendPumpEvent(const PumpEvent& e) {
  if (!ok_) return;
  // pumps.log grows unbounded on disk (harmless at project scale; rotate later if needed).
  File f = SD.open("/garden/pumps.log", FILE_APPEND);
  if (!f) return;
  f.write((const uint8_t*)&e, sizeof(e)); f.close();
}
int SdStorage::loadPumpEvents(PumpEvent* out, int maxOut) {
  if (!ok_) return 0;
  File f = SD.open("/garden/pumps.log", FILE_READ);
  if (!f) return 0;
  int k = 0;
  while (k < maxOut && f.available() >= (int)sizeof(PumpEvent)) {
    f.read((uint8_t*)&out[k], sizeof(PumpEvent)); k++;
  }
  f.close();
  return k;
}
void SdStorage::saveClock(uint32_t epoch) {
  if (!ok_) return;
  File f = SD.open("/garden/clock.dat", FILE_WRITE);
  if (!f) return;
  f.seek(0); f.write((const uint8_t*)&epoch, sizeof(epoch)); f.close();
}
bool SdStorage::loadClock(uint32_t& epoch) {
  if (!ok_) return false;
  File f = SD.open("/garden/clock.dat", FILE_READ);
  if (!f || f.size() < (int)sizeof(epoch)) { if (f) f.close(); return false; }
  f.read((uint8_t*)&epoch, sizeof(epoch)); f.close();
  return true;
}
