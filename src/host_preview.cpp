// Host preview harness (PlatformIO env:host_sim, platform = native).
//
// Renders pages with the SAME Canvas1 + page code that runs on the panel, then
// dumps each frame to a binary PGM so we can eyeball layouts without flashing.
// Excluded from the device build via build_src_filter.
#include <cstdio>
#include <cstdint>
#include "ui/canvas1.h"
#include "ui/ui_model.h"
#include "ui/pages.h"
#include "TimeSeriesStore.h"
#include "PumpLog.h"
#include "Clock.h"
#include "SimSource.h"
#include "data/GardenRepository.h"

static uint32_t g_simMillis = 0;
static uint32_t simMillis() { return g_simMillis; }

// Write the visible 792x272 area as a binary PGM (P5): 0 = black, 255 = white.
static bool writePGM(Canvas1 &c, const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "cannot open %s\n", path); return false; }
  fprintf(f, "P5\n%d %d\n255\n", Canvas1::W, Canvas1::H);
  for (int y = 0; y < Canvas1::H; y++)
    for (int x = 0; x < Canvas1::W; x++)
      fputc(c.getPixel(x, y) == Canvas1::BLACK ? 0 : 255, f);
  fclose(f);
  return true;
}

int main(int argc, char **argv) {
  const char *outDir = (argc > 1) ? argv[1] : "build/preview";

  TimeSeriesStore store; store.init();           // no storage -> pure RAM
  PumpLog log;
  Clock clock(simMillis);
  clock.setEpoch(1750000000);                    // a fixed plausible "now"
  uint32_t now = clock.now();

  SimSource sim; sim.seed(store, log, now);
  LocalRepository repo(store, log, clock);
  UiModel m = repo.buildModel();

  uint8_t frame[Canvas1::FRAME_BYTES];
  Canvas1 canvas(frame);
  char path[512];

  renderMain(canvas, m);
  snprintf(path, sizeof(path), "%s/main.pgm", outDir); writePGM(canvas, path);
  renderDetail(canvas, m);
  snprintf(path, sizeof(path), "%s/detail.pgm", outDir); writePGM(canvas, path);
  renderActuators(canvas, m, 1);
  snprintf(path, sizeof(path), "%s/actuators.pgm", outDir); writePGM(canvas, path);
  return 0;
}
