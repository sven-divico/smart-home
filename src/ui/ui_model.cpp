#include "ui_model.h"

// One fixed snapshot used everywhere (spec §6). Gewächshaus is dry (29% < 35%)
// and watering; everything else calm. Chart series derived from the mockup.
const UiModel &mockModel() {
  static const UiModel m = {
    "Dienstag, 16. Juni 2026",
    "14:23 Uhr",
    "14:23",
    // env
    { 22.4f, "Sonnig", 24, WX_SUN, 1013, TREND_UP, "Luftdruck steigt · stabil",
      58, 12000, 14, 25 },
    // chart: 7-day temp line + soil-moisture-average bars
    { { 16, 19, 18, 23, 21, 25, 24 },
      { 45, 43, 40, 38, 42, 39, 41 } },
    // nodes (Page 2)
    {
      { "Beet 1",                41, 18.2f, false, false, {44,43,43,42,42,41,41}, {46,45,44,43,42,41,41} },
      { "Beet 2",                37, 17.8f, false, false, {40,39,38,38,37,37,37}, {44,42,40,39,38,37,37} },
      { "Beet 3",                52, 16.9f, false, false, {49,50,50,51,51,52,52}, {47,48,49,50,51,52,52} },
      { "Gewächshaus Hochbeet",  29, 21.4f, true,  true,  {33,32,31,30,30,34,29}, {38,36,34,33,31,30,29} },
    },
    // pumps (Page 3)
    {
      { "Beet 1",               "PUMP-201", false, false, 41, 35, "AUTO", "zuletzt 06:30", nullptr,        4, 5 },
      { "Beet 2",               "PUMP-202", false, false, 37, 35, "AUTO", "zuletzt 08:10", nullptr,        3, 4 },
      { "Beet 3",               "PUMP-203", false, false, 52, 35, "AUTO", "zuletzt gestern", nullptr,      0, 3 },
      { "Gewächshaus Hochbeet", "PUMP-204", true,  true,  29, 35, "AUTO", "läuft · 00:02", "< Schwelle",   6, 7 },
    },
    false,                      // hasAlert
    "STURMWARNUNG bis 20:00 · Böen 75 km/h",
    1,                          // activePumps
  };
  return m;
}
