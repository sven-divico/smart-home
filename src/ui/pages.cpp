#include "pages.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fonts/FreeSerifBold24pt7b.h"
#include "fonts/FreeSerifBold18pt7b.h"
#include "fonts/FreeSerifBold12pt7b.h"
#include "fonts/FreeSerif9pt7b.h"
#include "fonts/FreeSans9pt7b.h"
#include "fonts/FreeSansBold9pt7b.h"
#include "fonts/FreeSansBold12pt7b.h"
#include "fonts/FreeSansBold18pt7b.h"
#include "fonts/FreeSansBold24pt7b.h"
#include "fonts/Picopixel.h"

static const uint8_t B = Canvas1::BLACK;
static const uint8_t W = Canvas1::WHITE;

// Content area is 758px; the physical buttons live in the 34px right rail.
static const int CONTENT_W = 758;
static const int MARGIN_L   = 22;
static const int MARGIN_R   = 734;   // right edge of content (matches mockup)
static const int RAIL_X     = 758;

// ---------------------------------------------------------------------------
// small drawing helpers (1-bit icons + bits the primitive set lacks)
// ---------------------------------------------------------------------------

static void fillDisc(Canvas1 &c, int cx, int cy, int r, uint8_t col) {
  for (int dy = -r; dy <= r; dy++) {
    int dx = (int)(sqrt((double)(r * r - dy * dy)) + 0.5);
    c.hLine(cx - dx, cy + dy, 2 * dx + 1, col);
  }
}

// Rounded-rect outline (used for the pump toggle, r = h/2 gives a pill).
static void roundRectOutline(Canvas1 &c, int x, int y, int w, int h, int r, uint8_t col) {
  c.hLine(x + r, y, w - 2 * r, col);
  c.hLine(x + r, y + h - 1, w - 2 * r, col);
  c.vLine(x, y + r, h - 2 * r, col);
  c.vLine(x + w - 1, y + r, h - 2 * r, col);
  int cx0 = x + r, cy0 = y + r, cx1 = x + w - 1 - r, cy1 = y + h - 1 - r;
  int px = 0, py = r, d = 3 - 2 * r;
  while (px <= py) {
    c.setPixel(cx0 - px, cy0 - py, col); c.setPixel(cx0 - py, cy0 - px, col);
    c.setPixel(cx1 + px, cy0 - py, col); c.setPixel(cx1 + py, cy0 - px, col);
    c.setPixel(cx0 - px, cy1 + py, col); c.setPixel(cx0 - py, cy1 + px, col);
    c.setPixel(cx1 + px, cy1 + py, col); c.setPixel(cx1 + py, cy1 + px, col);
    if (d < 0) d += 4 * px + 6; else { d += 4 * (px - py) + 10; py--; }
    px++;
  }
}

// Filled pill (centre rect + two end discs), r = h/2.
static void fillPill(Canvas1 &c, int x, int y, int w, int h, uint8_t col) {
  int r = h / 2;
  c.fillRect(x + r, y, w - 2 * r, h, col);
  fillDisc(c, x + r, y + r, r, col);
  fillDisc(c, x + w - 1 - r, y + r, r, col);
}

// 1-bit toggle switch. on = filled track + knob right; off = outline + knob left.
static void drawToggle(Canvas1 &c, int x, int y, bool on) {
  const int w = 44, h = 22, r = h / 2;
  if (on) {
    fillPill(c, x, y, w, h, B);
    fillDisc(c, x + w - 1 - r, y + r, r - 3, W);
  } else {
    roundRectOutline(c, x, y, w, h, r, B);
    fillDisc(c, x + r, y + r, r - 3, B);
  }
}

// ▶ play triangle (apex right), vertically centred on cy.
static void triRight(Canvas1 &c, int x, int cy, int s) {
  for (int k = 0; k <= s; k++) c.vLine(x + k, cy - (s - k), 2 * (s - k) + 1, B);
}

// Thick polyline (vertical thickness) — used for the temp line + its halo.
static void thickPoly(Canvas1 &c, const int *xs, const int *ys, int n, uint8_t col, int thick) {
  for (int i = 0; i < n - 1; i++)
    for (int t = -(thick / 2); t <= thick / 2; t++)
      c.line(xs[i], ys[i] + t, xs[i + 1], ys[i + 1] + t, col);
}

static void drawSun(Canvas1 &c, int cx, int cy, int r) {
  c.drawCircle(cx, cy, r, B);
  c.drawCircle(cx, cy, r - 1, B);
  for (int k = 0; k < 8; k++) {
    double a = k * M_PI / 4.0;
    int x0 = cx + (int)(cos(a) * (r + 4)), y0 = cy + (int)(sin(a) * (r + 4));
    int x1 = cx + (int)(cos(a) * (r + 10)), y1 = cy + (int)(sin(a) * (r + 10));
    c.line(x0, y0, x1, y1, B);
  }
}

// Pressure tendency arrow inside an s×s box anchored at lower-left (x, y).
static void drawTrendArrow(Canvas1 &c, int x, int y, Trend t, int s) {
  int tipx, tipy, tailx, taily;
  if (t == TREND_UP)        { tailx = x;     taily = y;       tipx = x + s; tipy = y - s; }
  else if (t == TREND_DOWN) { tailx = x;     taily = y - s;   tipx = x + s; tipy = y;     }
  else                      { tailx = x;     taily = y - s/2; tipx = x + s; tipy = y - s/2; }
  for (int o = 0; o < 2; o++) c.line(tailx, taily + o, tipx, tipy + o, B);  // 2px shaft
  // arrowhead: two short barbs back from the tip
  double ang = atan2((double)(tipy - taily), (double)(tipx - tailx));
  for (int sgn = -1; sgn <= 1; sgn += 2) {
    double a = ang + sgn * 2.5;  // ~140° off the shaft
    c.line(tipx, tipy, tipx + (int)(cos(a) * 8), tipy + (int)(sin(a) * 8), B);
  }
}

static void drawCheck(Canvas1 &c, int x, int y, int s) {
  for (int o = 0; o < 2; o++) {
    c.line(x, y + (int)(s * 0.45) + o, x + (int)(s * 0.35), y + s + o, B);
    c.line(x + (int)(s * 0.35), y + s + o, x + s, y + o, B);
  }
}

static void triUp(Canvas1 &c, int cx, int top, int h) {
  for (int k = 0; k <= h; k++) c.hLine(cx - k, top + k, 2 * k + 1, B);
}
static void triDown(Canvas1 &c, int cx, int top, int h) {
  for (int k = 0; k <= h; k++) c.hLine(cx - (h - k), top + k, 2 * (h - k) + 1, B);
}

// Byte length of a UTF-8 sequence from its lead byte.
static int utf8Len(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

// Draw text clipped to maxW, trimming whole codepoints and appending "…" (…as
// three dots) when it overflows. UTF-8 safe.
static void drawTrunc(Canvas1 &c, int x, int y, int maxW, const char *s, const GFXfont &f, uint8_t col) {
  if (c.textWidth(s, f) <= maxW) { c.drawText(x, y, s, f, col); return; }
  int ell = c.textWidth("...", f);
  char buf[160]; int bi = 0, w = 0;
  for (const char *p = s; *p; ) {
    int L = utf8Len((unsigned char)*p);
    char one[5]; for (int i = 0; i < L; i++) one[i] = p[i]; one[L] = 0;
    int cw = c.textWidth(one, f);
    if (w + cw + ell > maxW || bi + L + 4 > (int)sizeof(buf)) break;
    for (int i = 0; i < L; i++) buf[bi++] = p[i];
    w += cw; p += L;
  }
  buf[bi++] = '.'; buf[bi++] = '.'; buf[bi++] = '.'; buf[bi] = 0;
  c.drawText(x, y, buf, f, col);
}

// One sparkline: dotted baseline + 2px polyline. mn/mx is the shared per-tile
// vertical scale so a tile's 12 h and 7 d lines are directly comparable.
static void sparkline(Canvas1 &c, int x, int y, int w, int h,
                      const int8_t *vals, int n, int mn, int mx) {
  for (int i = 0; i < w; i += 3) c.setPixel(x + i, y + h - 1, B);   // faint dotted baseline
  if (mx <= mn) mx = mn + 1;
  int px[16], py[16];
  for (int i = 0; i < n; i++) {
    px[i] = x + (w - 1) * i / (n - 1);
    py[i] = y + 1 + (int)((h - 4) * (1.0 - (double)(vals[i] - mn) / (mx - mn)));
  }
  for (int i = 0; i < n - 1; i++) {
    c.line(px[i], py[i],     px[i + 1], py[i + 1],     B);
    c.line(px[i], py[i] + 1, px[i + 1], py[i + 1] + 1, B);
  }
}

// EIN/AUS pump badge, right-aligned to xRight. Filled (inverted) when on.
static void badge(Canvas1 &c, int xRight, int top, const char *txt, bool on) {
  int tw = c.textWidth(txt, Picopixel, 1);
  int bw = tw + 8, bh = 12, bx = xRight - bw;
  if (on) { c.fillRect(bx, top, bw, bh, B); c.drawText(bx + 4, top + 8, txt, Picopixel, W, 1); }
  else    { c.drawRect(bx, top, bw, bh, B); c.drawText(bx + 4, top + 8, txt, Picopixel, B, 1); }
}

static void pageDots(Canvas1 &c, int xRight, int cy, int active, uint8_t col) {
  const int r = 3, gap = 11;
  for (int i = 0; i < 3; i++) {
    int cx = xRight - (2 - i) * gap;
    c.drawCircle(cx, cy, r, col);
    if (i == active) fillDisc(c, cx, cy, r, col);
  }
}

// ---------------------------------------------------------------------------
// the always-present right control rail (mirrors the physical buttons)
// ---------------------------------------------------------------------------
static void renderRail(Canvas1 &c, bool wheelActive) {
  const int cx = RAIL_X + (Canvas1::W - RAIL_X) / 2;  // ~775
  c.vLine(RAIL_X, 0, Canvas1::H, B);

  const int RT = 1;  // rail tracking — Picopixel is tiny, 1px keeps it legible
  // top: Menü ▲ zurück
  triUp(c, cx, 8, 6);
  c.drawTextCentered(cx, 30, "MEN\xc3\x9c", Picopixel, B, RT);      // MENÜ
  c.drawTextCentered(cx, 38, "zur\xc3\xbck", Picopixel, B, RT);     // zurück

  // middle: wheel — hollow when idle, filled when active
  if (wheelActive) fillDisc(c, cx, 132, 6, B); else c.drawCircle(cx, 132, 6, B);
  if (wheelActive) {
    c.drawTextCentered(cx, 150, "drehen", Picopixel, B, RT);
    c.drawTextCentered(cx, 158, "w\xc3\xa4hlen", Picopixel, B, RT); // wählen
    c.drawTextCentered(cx, 166, "CONF", Picopixel, B, RT);
  } else {
    c.drawTextCentered(cx, 150, "Rad", Picopixel, B, RT);
    c.drawTextCentered(cx, 158, "frei", Picopixel, B, RT);
  }

  // bottom: Exit ▼ weiter
  triDown(c, cx, Canvas1::H - 22, 6);
  c.drawTextCentered(cx, Canvas1::H - 10, "EXIT", Picopixel, B, RT);
  c.drawTextCentered(cx, Canvas1::H - 3, "weiter", Picopixel, B, RT);
}

// ---------------------------------------------------------------------------
// Page 1 — Main
// ---------------------------------------------------------------------------
void renderMain(Canvas1 &c, const UiModel &m) {
  c.clear(W);

  // ---- masthead -----------------------------------------------------------
  c.drawText(MARGIN_L, 30, "DER GARTEN", FreeSerifBold18pt7b, B, 3);
  char dateline[80];
  snprintf(dateline, sizeof(dateline), "%s \xc2\xb7 %s", m.dateLine, m.clockLong); // · = U+00B7
  c.drawTextRight(MARGIN_R, 18, dateline, FreeSans9pt7b, B);
  pageDots(c, MARGIN_R, 33, 0, B);

  // ---- left block: weather + pressure tendency ----------------------------
  c.fillRect(MARGIN_L, 47, 300, 3, B);                 // 3px section rule

  char big[16];
  snprintf(big, sizeof(big), "%d\xc2\xb0", (int)(m.env.tempC + 0.5f)); // "22°"
  int afterTemp = c.drawText(MARGIN_L, 96, big, FreeSerifBold24pt7b, B);
  drawSun(c, afterTemp + 34, 74, 20);

  char cond[48];
  snprintf(cond, sizeof(cond), "%s \xc2\xb7 gef\xc3\xbchlt %d\xc2\xb0", m.env.condition, m.env.feelsLikeC);
  c.drawText(MARGIN_L, 122, cond, FreeSerif9pt7b, B);  // "Sonnig · gefühlt 24°"

  drawTrendArrow(c, MARGIN_L + 2, 158, m.env.pressureTrend, 24);
  char hpa[16];
  snprintf(hpa, sizeof(hpa), "%d hPa", m.env.pressureHpa);
  c.drawText(MARGIN_L + 40, 150, hpa, FreeSerifBold12pt7b, B);
  c.drawText(MARGIN_L + 40, 166, m.env.pressureWord, FreeSans9pt7b, B);

  // ---- combined dual-axis chart -------------------------------------------
  const int bx = 330, by = 50, bw = 404, bh = 122;
  c.drawRect(bx, by, bw, bh, B);
  c.drawRect(bx + 1, by + 1, bw - 2, bh - 2, B);       // ~1.5px frame

  // legend: °C  —— Temperatur   ▢ Bodenfeuchte Ø    %
  int ly = by + 12, lx = bx + 8;
  lx = c.drawText(lx, ly, "\xc2\xb0""C", Picopixel, B) + 6;
  c.hLine(lx, ly - 3, 12, B); c.hLine(lx, ly - 2, 12, B); lx += 16;       // line swatch
  lx = c.drawText(lx, ly, "Temperatur", Picopixel, B) + 12;
  c.drawRect(lx, ly - 6, 6, 6, B); lx += 10;                              // outlined-bar swatch
  c.drawText(lx, ly, "Bodenfeuchte \xc3\x98", Picopixel, B);             // Ø
  c.drawTextRight(bx + bw - 8, ly, "%", Picopixel, B);

  const int plotL = bx + 34, plotR = bx + bw - 22;
  const int plotT = by + 26, plotB = by + bh - 18;
  const int plotMid = (plotT + plotB) / 2;
  c.vLine(plotL, plotT, plotB - plotT, B);
  c.vLine(plotR, plotT, plotB - plotT, B);
  c.hLine(plotL, plotB, plotR - plotL, B);
  for (int x = plotL; x < plotR; x += 6) c.hLine(x, plotMid, 3, B);  // dashed mid gridline

  // axis labels (left °C 10..30, right % 0..60)
  c.drawTextRight(plotL - 4, plotT + 3, "30", Picopixel, B);
  c.drawTextRight(plotL - 4, plotMid + 3, "20", Picopixel, B);
  c.drawTextRight(plotL - 4, plotB + 3, "10", Picopixel, B);
  c.drawText(plotR + 4, plotT + 3, "60", Picopixel, B);
  c.drawText(plotR + 4, plotMid + 3, "30", Picopixel, B);
  c.drawText(plotR + 4, plotB + 3, "0", Picopixel, B);

  const int n = 7;
  const int slot = (plotR - plotL) / n;
  const int barW = slot - 14;
  int tx[7], ty[7];
  for (int i = 0; i < n; i++) {
    int cxs = plotL + slot * i + slot / 2;
    // soil bar (outlined), right axis 0..60%
    int bart = plotB - (int)((plotB - plotT) * (m.chart.soilPct[i] / 60.0f));
    c.drawRect(cxs - barW / 2, bart, barW, plotB - bart, B);
    // temp point, left axis 10..30°C
    tx[i] = cxs;
    ty[i] = plotB - (int)((plotB - plotT) * ((m.chart.tempC[i] - 10) / 20.0f));
  }
  thickPoly(c, tx, ty, n, W, 5);   // white knockout halo
  thickPoly(c, tx, ty, n, B, 2);   // black temp line
  fillDisc(c, tx[n - 1], ty[n - 1], 3, B);
  c.drawCircle(tx[n - 1], ty[n - 1], 4, W);

  // ---- message band -------------------------------------------------------
  const int mbx = 330, mby = 178, mbw = 404, mbh = 28;
  if (m.hasAlert) {
    c.fillRect(mbx, mby, mbw, mbh, B);
    c.drawText(mbx + 12, mby + 19, m.alertText, FreeSansBold9pt7b, W);
  } else {
    c.drawRect(mbx, mby, mbw, mbh, B);
    drawCheck(c, mbx + 12, mby + 8, 12);
    c.drawText(mbx + 32, mby + 19, "keine aktuellen Meldungen", FreeSans9pt7b, B);
  }

  // ---- footer -------------------------------------------------------------
  c.fillRect(MARGIN_L, 244, MARGIN_R - MARGIN_L, 1, B);
  int fx = MARGIN_L, fy = 262;
  const int NT = 1;  // numeric tracking — opens up the tight bold digits
  char num[16];
  snprintf(num, sizeof(num), "%d%%", m.env.humidityPct);
  fx = c.drawText(fx, fy, num, FreeSansBold9pt7b, B, NT);
  fx = c.drawText(fx + 4, fy, "Feuchte", FreeSans9pt7b, B) + 18;
  snprintf(num, sizeof(num), "%d", m.env.pressureHpa);
  fx = c.drawText(fx, fy, num, FreeSansBold9pt7b, B, NT);
  fx = c.drawText(fx + 4, fy, "hPa", FreeSans9pt7b, B) + 18;
  snprintf(num, sizeof(num), "%dk", m.env.lightLux / 1000);
  fx = c.drawText(fx, fy, num, FreeSansBold9pt7b, B, NT);
  fx = c.drawText(fx + 4, fy, "lux", FreeSans9pt7b, B);

  char beete[48];
  snprintf(beete, sizeof(beete), "%d\xc2\xb7%d\xc2\xb7%d\xc2\xb7%d%%",
           m.nodes[0].soilPct, m.nodes[1].soilPct, m.nodes[2].soilPct, m.nodes[3].soilPct);
  int bw2 = c.textWidth(beete, FreeSansBold9pt7b, NT);
  c.drawText(MARGIN_R - bw2, fy, beete, FreeSansBold9pt7b, B, NT);
  c.drawTextRight(MARGIN_R - bw2 - 4, fy, "Beete:", FreeSans9pt7b, B);

  renderRail(c, false);  // wheel idle on Page 1
}

// ---------------------------------------------------------------------------
// Page 2 — Detail (sensors)
// ---------------------------------------------------------------------------
void renderDetail(Canvas1 &c, const UiModel &m) {
  c.clear(W);

  // ---- inverted header bar -------------------------------------------------
  c.fillRect(0, 0, CONTENT_W, 30, B);
  c.drawText(14, 20, "DETAIL \xc2\xb7 SENSOREN", FreeSansBold9pt7b, W, 1);
  int clockW = c.textWidth(m.clock, FreeSans9pt7b);
  c.drawTextRight(744, 20, m.clock, FreeSans9pt7b, W);
  pageDots(c, 744 - clockW - 12, 15, 1, W);

  // 4 equal columns shared by the env row and the tiles (fixed grid).
  int bx[5];
  for (int i = 0; i <= 4; i++) bx[i] = (CONTENT_W * i) / 4;

  // ---- environment row -----------------------------------------------------
  const int envY = 34, envH = 56;
  // column dividers run continuously through the env row + tiles (one grid)
  for (int i = 1; i < 4; i++) c.vLine(bx[i], envY, 214, B);  // 34 -> 248
  const char *envLbl[4] = {"LUFTTEMP.", "FEUCHTE", "DRUCK", "LICHT"};
  char v0[16], v1[16], v2[16], v3[16];
  snprintf(v0, sizeof v0, "%.1f\xc2\xb0", m.env.tempC);
  snprintf(v1, sizeof v1, "%d%%", m.env.humidityPct);
  snprintf(v2, sizeof v2, "%d", m.env.pressureHpa);
  snprintf(v3, sizeof v3, "%dk", m.env.lightLux / 1000);
  const char *envVal[4] = {v0, v1, v2, v3};
  for (int i = 0; i < 4; i++) {
    int cx0 = bx[i] + 14;
    c.drawText(cx0, envY + 16, envLbl[i], Picopixel, B, 1);
    int ex = c.drawText(cx0, envY + 46, envVal[i], FreeSansBold18pt7b, B);
    if (i == 2) drawTrendArrow(c, ex + 8, envY + 44, m.env.pressureTrend, 14);  // ↗ after 1013
  }

  // ---- node tiles ----------------------------------------------------------
  const int tileY = 92, tileH = 156;
  for (int i = 0; i < 4; i++) {
    const Node &nd = m.nodes[i];
    int x = bx[i] + 9, right = bx[i + 1] - 9, innerW = right - x;

    // name (truncates) + EIN/AUS badge
    const char *bl = nd.pumpOn ? "EIN" : "AUS";
    badge(c, right, tileY + 6, bl, nd.pumpOn);
    int badgeW = c.textWidth(bl, Picopixel, 1) + 8;
    drawTrunc(c, x, tileY + 16, innerW - badgeW - 8, nd.name, FreeSansBold9pt7b, B);

    // big soil % + soil temp / "trocken"
    char pct[8]; snprintf(pct, sizeof pct, "%d", nd.soilPct);
    int sx = c.drawText(x, tileY + 54, pct, FreeSansBold24pt7b, B);
    sx = c.drawText(sx + 1, tileY + 54, "%", FreeSansBold12pt7b, B);
    char sub[24];
    if (nd.dry) snprintf(sub, sizeof sub, " \xc2\xb7 trocken");
    else        snprintf(sub, sizeof sub, " \xc2\xb7 %.1f\xc2\xb0", nd.soilTempC);
    c.drawText(sx + 4, tileY + 52, sub, FreeSans9pt7b, B);

    // sparklines — shared per-tile vertical scale across both windows
    int mn = 127, mx = -128;
    for (int k = 0; k < 7; k++) {
      int8_t a = nd.spark12h[k], b2 = nd.spark7d[k];
      if (a < mn) mn = a; if (a > mx) mx = a;
      if (b2 < mn) mn = b2; if (b2 > mx) mx = b2;
    }
    int spx = x + 36, spw = innerW - 36;
    c.drawText(x, tileY + 78, "12 STD", Picopixel, B, 1);
    sparkline(c, spx, tileY + 68, spw, 18, nd.spark12h, 7, mn, mx);
    c.drawText(x, tileY + 104, "7 TAGE", Picopixel, B, 1);
    sparkline(c, spx, tileY + 94, spw, 18, nd.spark7d, 7, mn, mx);
  }

  // ---- footer (subdued) ----------------------------------------------------
  c.hLine(0, tileY + tileH, CONTENT_W, B);  // y = 248
  c.drawText(14, 261, "aktualisiert vor 2 min", Picopixel, B, 1);
  c.drawTextRight(744, 261, "4 Knoten \xc2\xb7 alle OK", Picopixel, B, 1);

  renderRail(c, false);  // wheel idle on Page 2
}

// ---------------------------------------------------------------------------
// Page 3 — Actuators (pumps), interactive: `focus` is the highlighted row.
// ---------------------------------------------------------------------------
void renderActuators(Canvas1 &c, const UiModel &m, int focus) {
  c.clear(W);

  // ---- inverted header -----------------------------------------------------
  c.fillRect(0, 0, CONTENT_W, 30, B);
  int hx = c.drawText(14, 20, "AKTOREN \xc2\xb7 PUMPEN", FreeSansBold9pt7b, W, 1);
  char act[16]; snprintf(act, sizeof act, "%d aktiv", m.activePumps);
  c.drawText(hx + 12, 20, act, FreeSans9pt7b, W);
  int clockW = c.textWidth(m.clock, FreeSans9pt7b);
  c.drawTextRight(744, 20, m.clock, FreeSans9pt7b, W);
  pageDots(c, 744 - clockW - 12, 15, 2, W);

  // ---- grid: cursor gutter | toggle | name | moisture | mode | timing ------
  // Cursor and toggle are the two leftmost, adjacent columns, so EVERYTHING that
  // changes on interaction (cursor on focus, toggle on CONF) sits in one small
  // left strip — minimal partial-refresh area. No per-row status text, no edge bar;
  // a pump's running state is shown by the toggle alone.
  const int bodyTop = 30, rowH = 53;
  const int cToggle = 20, cName = 160, cMoist = 290, cMode = 536, cTime = 620;
  const int colDiv[5] = {cToggle, cName, cMoist, cMode, cTime};
  for (int k = 0; k < 5; k++) c.vLine(colDiv[k], bodyTop, 4 * rowH, B);
  for (int r = 1; r < 4; r++) c.hLine(0, bodyTop + r * rowH, CONTENT_W, B);

  for (int i = 0; i < 4; i++) {
    const Pump &p = m.pumps[i];
    int top = bodyTop + i * rowH;
    bool focused = (i == focus);

    // gutter: only the wheel-selection ▶ cursor
    if (focused) triRight(c, 8, top + rowH / 2, 6);

    // toggle only — state shown by the switch graphic alone (text dropped for the
    // partial-refresh test: a toggle then redraws just the small switch).
    int s1 = cToggle + 12;
    drawToggle(c, s1, top + 16, p.on);

    // name (fixed)
    drawTrunc(c, cName + 10, top + 24, cMoist - (cName + 10) - 8, p.zone, FreeSansBold12pt7b, B);
    c.drawText(cName + 10, top + 40, p.nodeId, Picopixel, B, 1);

    // moisture vs target
    int mx = cMoist + 12;
    char cp[8]; snprintf(cp, sizeof cp, "%d%%", p.currentPct);
    int mxr = c.drawText(mx, top + 22, cp, FreeSansBold12pt7b, B);
    char zl[16]; snprintf(zl, sizeof zl, "Ziel %d%%", p.targetPct);
    c.drawText(mxr + 8, top + 22, zl, Picopixel, B, 1);
    int barX = mx, barW = cMode - 12 - barX, barY = top + 30, barH = 9;
    c.drawRect(barX, barY, barW, barH, B);
    c.fillRect(barX + 1, barY + 1, (int)((barW - 2) * p.currentPct / 100.0), barH - 2, B);
    int tickX = barX + (int)((barW - 2) * p.targetPct / 100.0);
    c.vLine(tickX, barY - 3, barH + 6, B);  // target tick

    // mode
    c.drawText(cMode + 12, top + 22, p.mode, FreeSansBold9pt7b, B, 1);
    if (p.reason) c.drawText(cMode + 12, top + 36, p.reason, Picopixel, B, 1);

    // timing
    char th[24]; snprintf(th, sizeof th, "Heute %d min", p.minutesToday);
    c.drawText(cTime + 12, top + 21, th, FreeSans9pt7b, B);
    char av[16]; snprintf(av, sizeof av, "\xc3\x98 %d/Tag", p.avgPerDay);
    c.drawText(cTime + 12, top + 38, av, Picopixel, B, 1);
  }

  // ---- footer --------------------------------------------------------------
  c.hLine(0, bodyTop + 4 * rowH, CONTENT_W, B);  // y = 242
  c.drawText(14, 258, "Bew\xc3\xa4sserung automatisch \xc2\xb7 Schwelle 35%", Picopixel, B, 1);
  c.drawTextRight(744, 258, "Rad w\xc3\xa4hlt \xc2\xb7 CONF schaltet", Picopixel, B, 1);  // static CONF hint

  renderRail(c, true);  // wheel ACTIVE on Page 3
}
