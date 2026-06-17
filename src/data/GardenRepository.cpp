#include "GardenRepository.h"
#include "SimSource.h"     // DRY_THRESHOLD / WET_TARGET
#include <time.h>
#include <math.h>
#include <stdio.h>
using namespace ts;

static const char*  NODE_NAMES[4] = {"Beet 1", "Beet 2", "Beet 3", "Gewächshaus Hochbeet"};
static const NodeId SOIL_NODES[4] = {NODE_BEET1, NODE_BEET2, NODE_BEET3, NODE_GEWAECHSHAUS};

// string storage for the const char* fields of one UiModel
static char s_dateLine[48], s_clockLong[16], s_clock[8];
static char s_nodeId[4][12], s_statusLine[4][32];  // 32 leaves margin for long "läuft · MM:SS"

static int    iround(float v) { return (int)lroundf(v); }
static int8_t i8(float v) { if (v < -128) v = -128; if (v > 127) v = 127; return (int8_t)lroundf(v); }

void LocalRepository::fillDateTime(UiModel& m, uint32_t now) {
  time_t t = (time_t)now; struct tm tv; gmtime_r(&t, &tv);
  static const char* WD[7] = {"Sonntag","Montag","Dienstag","Mittwoch","Donnerstag","Freitag","Samstag"};
  static const char* MO[12] = {"Januar","Februar","März","April","Mai","Juni",
                               "Juli","August","September","Oktober","November","Dezember"};
  snprintf(s_dateLine, sizeof(s_dateLine), "%s, %d. %s %d",
           WD[tv.tm_wday], tv.tm_mday, MO[tv.tm_mon], tv.tm_year + 1900);
  snprintf(s_clockLong, sizeof(s_clockLong), "%02d:%02d Uhr", tv.tm_hour, tv.tm_min);
  snprintf(s_clock, sizeof(s_clock), "%02d:%02d", tv.tm_hour, tv.tm_min);
  m.dateLine = s_dateLine; m.clockLong = s_clockLong; m.clock = s_clock;
}

void LocalRepository::fillEnv(UiModel& m, uint32_t now) {
  EnvStation& e = m.env;
  float temp = store_.latest(NODE_ENV, M_AIR_TEMP); if (isnan(temp)) temp = 20;
  float hum  = store_.latest(NODE_ENV, M_HUMIDITY); if (isnan(hum))  hum  = 55;
  float lux  = store_.latest(NODE_ENV, M_LUX);      if (isnan(lux))  lux  = 0;
  float pres = store_.latest(NODE_ENV, M_PRESSURE); if (isnan(pres)) pres = 1013;
  e.tempC = temp; e.humidityPct = iround(hum); e.lightLux = iround(lux); e.pressureHpa = iround(pres);

  if      (lux < 500)  { e.glyph = WX_MOON;  e.condition = "Klar"; }
  else if (hum > 80)   { e.glyph = WX_RAIN;  e.condition = "Regen"; }
  else if (lux > 8000) { e.glyph = WX_SUN;   e.condition = "Sonnig"; }
  else                 { e.glyph = WX_CLOUD; e.condition = "Bewölkt"; }

  e.feelsLikeC = iround(temp + (hum > 65 ? 2.0f : 0.0f) - (hum < 30 ? 1.0f : 0.0f));

  int tr = store_.trend(NODE_ENV, M_PRESSURE, now);
  e.pressureTrend = (tr > 0 ? TREND_UP : (tr < 0 ? TREND_DOWN : TREND_STEADY));
  e.pressureWord  = (tr > 0 ? "Luftdruck steigt · stabil"
                            : (tr < 0 ? "Luftdruck fällt" : "Luftdruck stabil"));

  float lo, hi;
  if (store_.minMaxToday(NODE_ENV, M_AIR_TEMP, now, lo, hi)) { e.dayLoC = iround(lo); e.dayHiC = iround(hi); }
  else { e.dayLoC = iround(temp); e.dayHiC = iround(temp); }
}

void LocalRepository::fillChart(UiModel& m, uint32_t now) {
  float temp7[7]; store_.sampleWindow(NODE_ENV, M_AIR_TEMP, W_7D, now, temp7, 7);
  float soil7[7]; store_.averageAcrossNodes(M_SOIL, W_7D, now, soil7, 7);
  for (int i = 0; i < 7; i++) { m.chart.tempC[i] = temp7[i]; m.chart.soilPct[i] = iround(soil7[i]); }
}

void LocalRepository::fillNodes(UiModel& m, uint32_t now) {
  for (int i = 0; i < 4; i++) {
    Node& nd = m.nodes[i]; NodeId node = SOIL_NODES[i];
    float soil = store_.latest(node, M_SOIL);      if (isnan(soil)) soil = 40;
    float stmp = store_.latest(node, M_SOIL_TEMP); if (isnan(stmp)) stmp = 17;
    nd.name = NODE_NAMES[i];
    nd.soilPct = iround(soil); nd.soilTempC = stmp;
    nd.dry = nd.soilPct < SimSource::DRY_THRESHOLD;
    nd.pumpOn = log_.isRunning((uint8_t)node, now);
    float s12[7]; store_.sampleWindow(node, M_SOIL, W_12H, now, s12, 7);
    float s7[7];  store_.sampleWindow(node, M_SOIL, W_7D,  now, s7,  7);
    for (int k = 0; k < 7; k++) { nd.spark12h[k] = i8(s12[k]); nd.spark7d[k] = i8(s7[k]); }
  }
}

bool LocalRepository::lastStart(NodeId node, uint32_t& ts) const {
  bool found = false;
  for (uint16_t i = 0; i < log_.size(); i++) {
    const PumpEvent& e = log_.at(i);
    if (e.pumpId == (uint8_t)node && e.event == EV_START) { ts = e.ts; found = true; }
  }
  return found;
}
bool LocalRepository::lastStop(NodeId node, uint32_t& ts) const {
  bool found = false;
  for (uint16_t i = 0; i < log_.size(); i++) {
    const PumpEvent& e = log_.at(i);
    if (e.pumpId == (uint8_t)node && e.event == EV_STOP) { ts = e.ts; found = true; }
  }
  return found;
}

void LocalRepository::fillPumps(UiModel& m, uint32_t now) {
  int active = 0;
  for (int i = 0; i < 4; i++) {
    Pump& p = m.pumps[i]; NodeId node = SOIL_NODES[i];
    bool running = log_.isRunning((uint8_t)node, now);
    float soil = store_.latest(node, M_SOIL); if (isnan(soil)) soil = 40;
    p.zone = NODE_NAMES[i];
    snprintf(s_nodeId[i], sizeof(s_nodeId[i]), "PUMP-20%d", i + 1);
    p.nodeId = s_nodeId[i];
    p.on = running; p.active = running;
    p.currentPct = iround(soil); p.targetPct = SimSource::WET_TARGET; p.mode = "AUTO";
    p.minutesToday = log_.minutesToday((uint8_t)node, now);
    p.avgPerDay    = log_.avgPerDay((uint8_t)node, now, 7);
    if (running) {
      uint32_t since = 0, dur = 0;   // init: lastStart always succeeds while running, but keep it defined
      if (lastStart(node, since)) dur = (now > since) ? now - since : 0;
      snprintf(s_statusLine[i], sizeof(s_statusLine[i]), "läuft · %02u:%02u",
               (unsigned)(dur / 60), (unsigned)(dur % 60));
      p.reason = "< Schwelle";
    } else {
      uint32_t stopTs;
      if (lastStop(node, stopTs)) {
        time_t t = (time_t)stopTs; struct tm tv; gmtime_r(&t, &tv);
        snprintf(s_statusLine[i], sizeof(s_statusLine[i]), "zuletzt %02d:%02d", tv.tm_hour, tv.tm_min);
      } else snprintf(s_statusLine[i], sizeof(s_statusLine[i]), "–");
      p.reason = nullptr;
    }
    p.statusLine = s_statusLine[i];
    if (running) active++;
  }
  m.activePumps = active;
}

UiModel LocalRepository::buildModel() {
  uint32_t now = clock_.now();
  UiModel m{};                       // zero-init; const char* fields set below
  fillDateTime(m, now);
  fillEnv(m, now);
  fillChart(m, now);
  fillNodes(m, now);
  fillPumps(m, now);
  m.hasAlert = false;  // TODO(later mission): wire a real alert source; alertText unused while false
  m.alertText = "STURMWARNUNG bis 20:00 · Böen 75 km/h";
  return m;
}

void LocalRepository::togglePump(int index) {
  if (index < 0 || index >= 4) return;
  NodeId node = SOIL_NODES[index];
  uint32_t now = clock_.now();
  float soil = store_.latest(node, M_SOIL); if (isnan(soil)) soil = 40;
  int16_t soilPct = (int16_t)lroundf(soil);
  if (log_.isRunning((uint8_t)node, now))
    log_.append({now, (uint8_t)node, EV_STOP,  soilPct, SimSource::WET_TARGET});
  else
    log_.append({now, (uint8_t)node, EV_START, soilPct, SimSource::DRY_THRESHOLD});
}
