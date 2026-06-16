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
  const UiModel &m = mockModel();

  uint8_t frame[Canvas1::FRAME_BYTES];
  Canvas1 canvas(frame);

  char path[512];

  renderMain(canvas, m);
  snprintf(path, sizeof(path), "%s/main.pgm", outDir);
  if (!writePGM(canvas, path)) return 1;
  printf("wrote %s\n", path);

  renderDetail(canvas, m);
  snprintf(path, sizeof(path), "%s/detail.pgm", outDir);
  if (!writePGM(canvas, path)) return 1;
  printf("wrote %s\n", path);

  renderActuators(canvas, m, /*focus=*/1);  // Beet 2 focused, matching the mockup
  snprintf(path, sizeof(path), "%s/actuators.pgm", outDir);
  if (!writePGM(canvas, path)) return 1;
  printf("wrote %s\n", path);

  return 0;
}
