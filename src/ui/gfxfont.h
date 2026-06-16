#pragma once
// Minimal re-declaration of Adafruit's GFXfont format so we can reuse their
// (MIT-licensed) font *data* files without pulling in the whole Adafruit GFX
// library. The vendored font headers in fonts/ are pointed at this file.
//
// Why this works on both targets:
//   * On ESP32 flash is memory-mapped, so a PROGMEM array can be read with a
//     plain pointer dereference (unlike AVR). On the host PROGMEM is a no-op.
//   * Hence pgm_read_byte is just a dereference everywhere. The #ifndef guards
//     keep us out of the way if the Arduino core already defined these.
#include <stdint.h>

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#endif

// One glyph: where its bitmap lives, its box, and how far to advance the cursor.
typedef struct {
  uint16_t bitmapOffset;  // start of bitmap bits in the font's bitmap[] blob
  uint8_t  width;         // bitmap dimensions in pixels
  uint8_t  height;
  uint8_t  xAdvance;      // cursor advance after drawing this glyph
  int8_t   xOffset;       // x/y offset of the bitmap box from the cursor/baseline
  int8_t   yOffset;
} GFXglyph;

// One font: a packed 1-bpp bitmap blob + a glyph table for a contiguous range.
typedef struct {
  uint8_t  *bitmap;    // glyph bitmap bits, MSB-first, packed across glyphs
  GFXglyph *glyph;     // glyph table, indexed by (codepoint - first)
  uint16_t  first;     // first codepoint covered (usually 0x20 space)
  uint16_t  last;      // last codepoint covered (usually 0x7E '~')
  uint8_t   yAdvance;  // line height (baseline-to-baseline)
} GFXfont;
