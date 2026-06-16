#include "canvas1.h"
#include <string.h>
#include <stdlib.h>  // abs

// --- low-level pixel access -------------------------------------------------

void Canvas1::clear(uint8_t color) {
  // WHITE = all bits set (0xFF), BLACK = all clear. Fills the seam too (harmless).
  memset(buf_, color == WHITE ? 0xFF : 0x00, FRAME_BYTES);
}

void Canvas1::setPixel(int x, int y, uint8_t color) {
  if (!inBounds(x, y)) return;
  int X = (x >= SEAM_X) ? x + SEAM_GAP : x;          // hop over the dead seam
  uint32_t addr = (X >> 3) + y * BYTES_PER_ROW;
  uint8_t  mask = 0x80 >> (X & 7);                   // MSB = leftmost pixel
  if (color == BLACK) buf_[addr] &= ~mask;           // ink -> clear bit (matches driver)
  else                buf_[addr] |=  mask;           // white -> set bit
}

// Reverse the 8 bits of a byte (so a byte's pixels flip left-right).
static inline uint8_t revBits(uint8_t b) {
  b = (uint8_t)((b & 0xF0) >> 4 | (b & 0x0F) << 4);
  b = (uint8_t)((b & 0xCC) >> 2 | (b & 0x33) << 2);
  b = (uint8_t)((b & 0xAA) >> 1 | (b & 0x55) << 1);
  return b;
}

void Canvas1::rotate180() {
  // 180° rotation == reverse the full MSB-first pixel bitstream: new_byte[i] =
  // revBits(old_byte[last - i]). Done in place by swapping symmetric pairs.
  for (int i = 0; i < FRAME_BYTES / 2; i++) {
    uint8_t a = buf_[i], b = buf_[FRAME_BYTES - 1 - i];
    buf_[i] = revBits(b);
    buf_[FRAME_BYTES - 1 - i] = revBits(a);
  }
}

uint8_t Canvas1::getPixel(int x, int y) const {
  if (!inBounds(x, y)) return WHITE;
  int X = (x >= SEAM_X) ? x + SEAM_GAP : x;
  uint32_t addr = (X >> 3) + y * BYTES_PER_ROW;
  uint8_t  mask = 0x80 >> (X & 7);
  return (buf_[addr] & mask) ? WHITE : BLACK;
}

// --- primitives -------------------------------------------------------------

void Canvas1::hLine(int x, int y, int w, uint8_t color) {
  for (int i = 0; i < w; i++) setPixel(x + i, y, color);
}

void Canvas1::vLine(int x, int y, int h, uint8_t color) {
  for (int i = 0; i < h; i++) setPixel(x, y + i, color);
}

void Canvas1::line(int x0, int y0, int x1, int y1, uint8_t color) {
  int dx =  abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    setPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void Canvas1::drawRect(int x, int y, int w, int h, uint8_t color) {
  hLine(x, y, w, color);
  hLine(x, y + h - 1, w, color);
  vLine(x, y, h, color);
  vLine(x + w - 1, y, h, color);
}

void Canvas1::fillRect(int x, int y, int w, int h, uint8_t color) {
  for (int j = 0; j < h; j++) hLine(x, y + j, w, color);
}

void Canvas1::drawCircle(int cx, int cy, int r, uint8_t color) {
  // Midpoint circle (hollow outline).
  int x = 0, y = r, d = 3 - 2 * r;
  while (x <= y) {
    setPixel(cx + x, cy + y, color); setPixel(cx - x, cy + y, color);
    setPixel(cx + x, cy - y, color); setPixel(cx - x, cy - y, color);
    setPixel(cx + y, cy + x, color); setPixel(cx - y, cy + x, color);
    setPixel(cx + y, cy - x, color); setPixel(cx - y, cy - x, color);
    if (d < 0) d += 4 * x + 6;
    else { d += 4 * (x - y) + 10; y--; }
    x++;
  }
}

void Canvas1::invertRect(int x, int y, int w, int h) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++)
      setPixel(x + i, y + j, getPixel(x + i, y + j) == BLACK ? WHITE : BLACK);
}

// --- text (Adafruit GFXfont format) -----------------------------------------

int Canvas1::drawChar(int x, int y, uint8_t c, const GFXfont &font, uint8_t color) {
  if (c < font.first || c > font.last) return 0;
  const GFXglyph *g = &font.glyph[c - font.first];
  const uint8_t  *bmp = font.bitmap;
  uint16_t bo = g->bitmapOffset;
  uint8_t  bits = 0, bit = 0;
  for (uint8_t yy = 0; yy < g->height; yy++) {
    for (uint8_t xx = 0; xx < g->width; xx++) {
      if ((bit++ & 7) == 0) bits = pgm_read_byte(&bmp[bo++]);
      if (bits & 0x80) setPixel(x + g->xOffset + xx, y + g->yOffset + yy, color);
      bits <<= 1;
    }
  }
  return g->xAdvance;
}

// Map an umlaut codepoint to the ASCII vowel we draw the diaeresis over.
static char umlautBase(uint32_t cp) {
  switch (cp) {
    case 0x00E4: return 'a';  case 0x00F6: return 'o';  case 0x00FC: return 'u';
    case 0x00C4: return 'A';  case 0x00D6: return 'O';  case 0x00DC: return 'U';
  }
  return 0;
}

// Decode one UTF-8 sequence and advance p past it. Returns U+FFFD on bad input.
uint32_t Canvas1::nextCodepoint(const char *&p) {
  uint8_t c = (uint8_t)*p++;
  if (c < 0x80) return c;
  uint32_t cp; int extra;
  if      ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
  else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
  else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
  else return 0xFFFD;
  for (int i = 0; i < extra; i++) {
    if ((*p & 0xC0) != 0x80) return 0xFFFD;
    cp = (cp << 6) | (uint8_t)(*p++ & 0x3F);
  }
  return cp;
}

int Canvas1::advanceFor(uint32_t cp, const GFXfont &font) const {
  if (cp >= font.first && cp <= font.last) return font.glyph[cp - font.first].xAdvance;
  char b = umlautBase(cp);
  if (b)             return font.glyph[(uint8_t)b - font.first].xAdvance;
  if (cp == 0x00B0)  { int r = font.yAdvance / 9; if (r < 2) r = 2; return 2 * r + 3; } // °
  if (cp == 0x00B7)  return font.yAdvance / 4 + 3;                                       // ·
  if (cp == 0x00DF)  return 2 * font.glyph[(uint8_t)'s' - font.first].xAdvance;          // ß -> ss
  if (cp == 0x00D8)  return font.glyph[(uint8_t)'O' - font.first].xAdvance;              // Ø
  if (cp == 0x00F8)  return font.glyph[(uint8_t)'o' - font.first].xAdvance;              // ø
  return 0;
}

int Canvas1::drawCodepoint(int x, int y, uint32_t cp, const GFXfont &font, uint8_t color) {
  if (cp >= font.first && cp <= font.last) return drawChar(x, y, (uint8_t)cp, font, color);

  if (char b = umlautBase(cp)) {
    int adv = drawChar(x, y, (uint8_t)b, font, color);
    const GFXglyph *g = &font.glyph[(uint8_t)b - font.first];
    int dw   = font.yAdvance >= 40 ? 3 : (font.yAdvance >= 20 ? 2 : 1);  // dot size
    int dotY = y + g->yOffset - dw - 1;                                  // just above the glyph top
    int cx   = x + g->xOffset + g->width / 2;
    int sp   = dw + (font.yAdvance >= 20 ? 2 : 1);                       // half-spacing
    fillRect(cx - sp,      dotY, dw, dw, color);
    fillRect(cx + sp - dw, dotY, dw, dw, color);
    return adv;
  }
  if (cp == 0x00B0) {  // degree sign -> small ring near the cap line
    int r = font.yAdvance / 9; if (r < 2) r = 2;
    drawCircle(x + r + 1, y - (int)(font.yAdvance * 0.62), r, color);
    return 2 * r + 3;
  }
  if (cp == 0x00B7) {  // middot -> small filled square mid-height
    int d = 2;
    fillRect(x + 2, y - (int)(font.yAdvance * 0.30), d, d, color);
    return font.yAdvance / 4 + 3;
  }
  if (cp == 0x00DF) {  // ß -> ss
    int adv = drawChar(x, y, 's', font, color);
    return adv + drawChar(x + adv, y, 's', font, color);
  }
  if (cp == 0x00D8 || cp == 0x00F8) {  // Ø / ø (the "average" symbol) -> O/o + slash
    char b = (cp == 0x00D8) ? 'O' : 'o';
    int adv = drawChar(x, y, (uint8_t)b, font, color);
    const GFXglyph *g = &font.glyph[(uint8_t)b - font.first];
    line(x + g->xOffset, y + g->yOffset + g->height, x + g->xOffset + g->width, y + g->yOffset, color);
    return adv;
  }
  return 0;  // unsupported: skip
}

int Canvas1::drawText(int x, int y, const char *s, const GFXfont &font, uint8_t color, int track) {
  const char *p = s;
  while (*p) x += drawCodepoint(x, y, nextCodepoint(p), font, color) + track;
  return x;
}

int Canvas1::textWidth(const char *s, const GFXfont &font, int track) const {
  int w = 0;
  const char *p = s;
  while (*p) w += advanceFor(nextCodepoint(p), font) + track;
  return w;
}

int Canvas1::drawTextRight(int xRight, int y, const char *s, const GFXfont &font, uint8_t color, int track) {
  int x = xRight - textWidth(s, font, track);
  drawText(x, y, s, font, color, track);
  return x;
}

int Canvas1::drawTextCentered(int xCenter, int y, const char *s, const GFXfont &font, uint8_t color, int track) {
  int x = xCenter - textWidth(s, font, track) / 2;
  drawText(x, y, s, font, color, track);
  return x;
}
