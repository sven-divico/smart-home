#pragma once
// Canvas1 — a 1-bit drawing surface for the CrowPanel 5.79" e-paper.
//
// The buffer layout is *identical* to the driver's native framebuffer
// (800x272, 100 bytes/row, MSB = leftmost pixel, bit 1 = white / bit 0 = ink),
// including the 8-px dead seam between the panel's two controllers. Render code
// uses clean logical coordinates 0..791 x 0..271; Canvas1 hides the seam.
//
// Because the layout matches the driver byte-for-byte, there is no "blit":
//   * on the panel  -> construct over the driver's s_frame, draw, EPD_Display(s_frame)
//   * on the host   -> construct over a local buffer, draw, dump to PGM/PNG
// Same drawing code, two sinks.
#include <stdint.h>
#include "gfxfont.h"

class Canvas1 {
public:
  // Logical (visible) geometry — what render code and the preview see.
  static const int W = 792;   // visible width
  static const int H = 272;   // height
  // Physical memory geometry — the driver's native buffer.
  static const int MEM_W       = 800;            // 792 visible + 8 seam
  static const int SEAM_X      = 396;            // logical x >= SEAM_X shifts +8
  static const int SEAM_GAP    = 8;
  static const int BYTES_PER_ROW = MEM_W / 8;    // 100
  static const int FRAME_BYTES   = BYTES_PER_ROW * H; // 27200

  // Logical pixel values (match the driver: BLACK clears a bit, WHITE sets it).
  static const uint8_t BLACK = 0;
  static const uint8_t WHITE = 1;

  // Wrap a caller-owned 27200-byte buffer (device: s_frame; host: local array).
  explicit Canvas1(uint8_t *buffer) : buf_(buffer) {}

  uint8_t *buffer() { return buf_; }

  void    clear(uint8_t color = WHITE);
  void    setPixel(int x, int y, uint8_t color);
  uint8_t getPixel(int x, int y) const;   // returns BLACK/WHITE for the visible pixel

  // Rotate the backing buffer 180° in place (panel-mounting compensation). A
  // full-buffer 180° flip = reversing the whole pixel bitstream, which conveniently
  // maps the centred 8-px seam onto itself. Device-only; the sim stays upright.
  void    rotate180();

  // Primitives (all seam-aware, all clipped to the visible area).
  void hLine(int x, int y, int w, uint8_t color = BLACK);
  void vLine(int x, int y, int h, uint8_t color = BLACK);
  void line(int x0, int y0, int x1, int y1, uint8_t color = BLACK);
  void drawRect(int x, int y, int w, int h, uint8_t color = BLACK);
  void fillRect(int x, int y, int w, int h, uint8_t color = BLACK);
  void drawCircle(int cx, int cy, int r, uint8_t color = BLACK);
  void invertRect(int x, int y, int w, int h);   // flip every pixel in the box

  // Text (Adafruit GFXfont format). Cursor (x,y) is the baseline-left origin.
  // drawText/textWidth are UTF-8 aware: glyphs the ASCII fonts lack are
  // synthesized — ° (ring), · (mid-dot), ä/ö/ü/Ä/Ö/Ü (base + dots), ß (ss).
  // `track` adds N px of extra spacing after each glyph (these small fonts have
  // near-zero side bearing, so wide adjacent digits otherwise touch).
  int  drawChar(int x, int y, uint8_t c, const GFXfont &font, uint8_t color = BLACK);
  int  drawText(int x, int y, const char *s, const GFXfont &font, uint8_t color = BLACK, int track = 0); // returns end x
  int  textWidth(const char *s, const GFXfont &font, int track = 0) const;
  int  drawTextRight(int xRight, int y, const char *s, const GFXfont &font, uint8_t color = BLACK, int track = 0);
  int  drawTextCentered(int xCenter, int y, const char *s, const GFXfont &font, uint8_t color = BLACK, int track = 0);

private:
  uint8_t *buf_;
  static bool inBounds(int x, int y) { return x >= 0 && x < W && y >= 0 && y < H; }

  // UTF-8 + synthesized-glyph plumbing (shared by drawText and textWidth).
  static uint32_t nextCodepoint(const char *&p);          // decode one seq, advance p
  int  drawCodepoint(int x, int y, uint32_t cp, const GFXfont &font, uint8_t color);
  int  advanceFor(uint32_t cp, const GFXfont &font) const; // must match drawCodepoint's advance
};
