/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "teletext.h"

#define TTX_ROWS 24
#define TTX_COLS 40
#define TTX_GAP_MS 500      /* rows further apart start a new subtitle */
#define TTX_MIN_MS 1200     /* on-screen floor when nothing follows */
#define TTX_MAX_MS 7000     /* cap, subtitles carry no clear signal */

/* G0 national subsets, Latin region 0 (EN 300 706 table 36). 13 differing code
 * points, UTF-8 escaped, sucks on purpose */
static const char *const g0_nat[8][13] = {
    /* 0x23 0x24 0x40 0x5B 0x5C 0x5D 0x5E 0x5F 0x60 0x7B 0x7C 0x7D 0x7E */
    {"#", "$", "\xC2\xA7", "\xC3\x84", "\xC3\x96", "\xC3\x9C", "^", "_", "\xC2\xB0", "\xC3\xA4", "\xC3\xB6", "\xC3\xBC", "\xC3\x9F"}, /* ger */
    {"\xC3\xA9", "\xC3\xAF", "\xC3\xA0", "\xC3\xAB", "\xC3\xAA", "\xC3\xB9", "\xC3\xAE", "#", "\xC3\xA8", "\xC3\xA2", "\xC3\xB4", "\xC3\xBB", "\xC3\xA7"}, /* fre */
  {"\xC2\xA3", "$", "\xC3\xA9", "\xC2\xB0", "\xC3\xA7", "\xE2\x86\x92", "\xE2\x86\x91", "#", "\xC3\xB9", "\xC3\xA0", "\xC3\xB2", "\xC3\xA8", "\xC3\xAC"}, /* ita */
    {"\xC2\xA3", "$", "@", "\xE2\x86\x90", "\xC2\xBD", "\xE2\x86\x92", "\xE2\x86\x91", "#", "\xE2\x80\x95", "\xC2\xBC", "\xE2\x80\x96", "\xC2\xBE", "\xC3\xB7"}, /* eng */
    {"#", "\xC2\xA4", "\xC3\x89", "\xC3\x84", "\xC3\x96", "\xC3\x85", "\xC3\x9C", "_", "\xC3\xA9", "\xC3\xA4", "\xC3\xB6", "\xC3\xA5", "\xC3\xBC"}, /* swe */
    {"\xC3\xA7", "$", "\xC2\xA1", "\xC3\xA1", "\xC3\xA9", "\xC3\xAD", "\xC3\xB3", "\xC3\xBA", "\xC2\xBF", "\xC3\xBC", "\xC3\xB1", "\xC3\xA8", "\xC3\xA0"}, /* pte/esp */
    {"#", "u", "\xC4\x8D", "\xC5\xA5", "\xC5\xBE", "\xC3\xBD", "\xC3\xAD", "\xC5\x99", "\xC3\xA9", "\xC3\xA1", "\xC4\x9B", "\xC3\xBA", "\xC5\xA1"}, /* cze */
    {"\xC2\xA3", "$", "@", "\xE2\x86\x90", "\xC2\xBD", "\xE2\x86\x92", "\xE2\x86\x91", "#", "\xE2\x80\x95", "\xC2\xBC", "\xE2\x80\x96", "\xC2\xBE", "\xC3\xB7"} /* dunno */
};
static const unsigned char g0_nat_pos[13] = {0x23, 0x24, 0x40, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x7B, 0x7C, 0x7D, 0x7E};

/* g0_nat row order, NOT the EN 300 706 subset numbers */
enum { G0_GER, G0_FRE, G0_ITA, G0_ENG, G0_SWE, G0_ESP, G0_CZE, G0_OTHER };

struct ttx {
  unsigned page;     /* target page, e.g. 777 */
  unsigned magazine; /* derived from page */
  ttx_cb cb;
  void *ctx;
  int nat;                              /* g0_nat row */
  int64_t lead;                         /* shift cues earlier, ms */
  char row[TTX_ROWS][TTX_COLS * 4 + 1]; /* arrival order */
  int nrows;
  int64_t group_start, group_last;      /* current row group */
  int64_t last_ms;
  char cur[TTX_TEXT_MAX]; /* on screen now */
  int64_t cur_start;
  int have_cur;
};

/* teletext bytes: LSB first (EN 300 706 7.1) */
static unsigned char rev8(unsigned char b) {
  b = (unsigned char)((b >> 4) | (b << 4));
  b = (unsigned char)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
  b = (unsigned char)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
  return b;
}

/* hamming 8/4, reversed orientation. 1-bit correcting, -1 otherwise */
static signed char unham[256];
static int unham_ready;

static void unham_init(void) {
  unsigned char code[16];
  int d, i, k;

  for (d = 0; d < 16; d++) {
    unsigned D1 = d & 1, D2 = (d >> 1) & 1, D3 = (d >> 2) & 1, D4 = (d >> 3) & 1;
    unsigned P1 = D1 ^ D2 ^ D4, P2 = D1 ^ D3 ^ D4, P3 = D2 ^ D3 ^ D4;
    unsigned c = P1 | (P2 << 1) | (D1 << 2) | (P3 << 3) | (D2 << 4) | (D3 << 5) | (D4 << 6);
    unsigned ones = 0;
    for (k = 0; k < 8; k++)
      ones += (c >> k) & 1;
    c |= (ones & 1) << 7; /* P4: even parity */
    code[d] = rev8((unsigned char)c);
  }
  for (i = 0; i < 256; i++) {
    int best = -1, bestd = 9, d2;
    for (d2 = 0; d2 < 16; d2++) {
      int diff = i ^ code[d2], n = 0;
      for (k = 0; k < 8; k++)
        n += (diff >> k) & 1;
      if (n < bestd) {
        bestd = n;
        best = d2;
      }
    }
    unham[i] = (bestd <= 1) ? (signed char)best : (signed char)-1;
  }
  unham_ready = 1;
}

static void append_utf8(char *dst, size_t cap, size_t *o, const char *s) {
  size_t n = strlen(s);
  if (*o + n + 1 < cap) {
    memcpy(dst + *o, s, n);
    *o += n;
  }
}

/* teletext bytes -> UTF-8; controls -> space */
static void row_text(const unsigned char *d, int cols, int nat, char *out, size_t cap) {
  size_t o = 0;
  int i, k;
  for (i = 0; i < cols; i++) {
    unsigned char c = d[i] & 0x7F;
    const char *rep = NULL;
    if (c < 0x20) { /* spacing attribute */
      if (o + 2 < cap)
        out[o++] = ' ';
      continue;
    }
    for (k = 0; k < 13; k++)
      if (g0_nat_pos[k] == c) {
        rep = g0_nat[nat & 7][k];
        break;
      }
    if (rep)
      append_utf8(out, cap, &o, rep);
    else if (o + 2 < cap)
      out[o++] = (char)c;
  }
  while (o && out[o - 1] == ' ') /* trim trailing */
    o--;
  out[o] = '\0';
}

static void build_text(ttx_t *t, char *out, size_t cap) {
  size_t o = 0;
  int r;

  for (r = 0; r < t->nrows; r++) {
    const char *s = t->row[r];
    size_t n;
    while (*s == ' ') /* trim leading */
      s++;
    n = strlen(s);
    if (!n)
      continue;
    if (o && o + 1 < cap)
      out[o++] = '\n';
    if (o + n + 1 < cap) {
      memcpy(out + o, s, n);
      o += n;
    }
  }
  out[o] = '\0';
}

static void emit_cue(ttx_t *t, int64_t end) {
  ttx_cue_t cue;

  if (!t->have_cur || !t->cur[0])
    return;
  if (end > t->cur_start + TTX_MAX_MS) /* no clear signal, do not linger */
    end = t->cur_start + TTX_MAX_MS;
  if (end <= t->cur_start)
    return;
  cue.start_ms = t->cur_start - t->lead;
  cue.end_ms = end - t->lead;
  if (cue.start_ms < 0)
    cue.start_ms = 0;
  snprintf(cue.text, sizeof cue.text, "%s", t->cur);
  t->cb(t->ctx, &cue);
}

/* row group finished: new subtitle, or the same one retransmitted */
static void group_done(ttx_t *t) {
  char text[TTX_TEXT_MAX];

  build_text(t, text, sizeof text);
  t->nrows = 0;
  if (!text[0])
    return;
  if (t->have_cur && strcmp(text, t->cur) == 0)
    return;                  /* carousel repeat */
  emit_cue(t, t->group_start); /* previous ends where this begins */
  snprintf(t->cur, sizeof t->cur, "%s", text);
  t->cur_start = t->group_start;
  t->have_cur = 1;
}

/* subtitle pages: display rows in EBU subtitle units, no page-0 header.
   per-cycle ident row (leads with page number) = boundary, not text */
static int is_ident_row(const ttx_t *t, const char *text) {
  char pg[8];
  size_t n;

  snprintf(pg, sizeof pg, "%u", t->page);
  n = strlen(pg);
  while (*text == ' ')
    text++;
  return strncmp(text, pg, n) == 0;
}

/* rows of one subtitle arrive together; a gap starts the next */
static void add_row(ttx_t *t, const unsigned char *d, int64_t ts) {
  char text[TTX_COLS * 4 + 1];
  int r;

  row_text(d, TTX_COLS, t->nat, text, sizeof text);
  if (!text[0])
    return;
  if (t->nrows && ts - t->group_last > TTX_GAP_MS)
    group_done(t);
  if (!t->nrows)
    t->group_start = ts;
  t->group_last = ts;
  for (r = 0; r < t->nrows; r++)
    if (strcmp(t->row[r], text) == 0)
      return; /* already in this group */
  if (t->nrows >= TTX_ROWS)
    return;
  snprintf(t->row[t->nrows], sizeof t->row[0], "%s", text);
  t->nrows++;
}

static void handle_packet(ttx_t *t, unsigned mag, unsigned pkt, const unsigned char *d, int64_t ts) {
  char text[TTX_COLS * 4 + 1];

  (void)mag;
  (void)pkt;
  /* ident row: cols 0-7 coded, read from 8 */
  row_text(d + 8, TTX_COLS - 8, t->nat, text, sizeof text);
  if (is_ident_row(t, text))
    return;
  add_row(t, d, ts);
}

/* teletext language -> g0_nat row */
static int nat_from_lang(const char *lang) {
  if (!lang || !*lang)
    return G0_ENG;
  if (!strncmp(lang, "deu", 3) || !strncmp(lang, "ger", 3))
    return G0_GER;
  if (!strncmp(lang, "fra", 3) || !strncmp(lang, "fre", 3))
    return G0_FRE;
  if (!strncmp(lang, "ita", 3))
    return G0_ITA;
  if (!strncmp(lang, "swe", 3) || !strncmp(lang, "fin", 3) || !strncmp(lang, "hun", 3))
    return G0_SWE;
  if (!strncmp(lang, "por", 3) || !strncmp(lang, "spa", 3))
    return G0_ESP;
  if (!strncmp(lang, "ces", 3) || !strncmp(lang, "cze", 3) || !strncmp(lang, "slk", 3) || !strncmp(lang, "slo", 3))
    return G0_CZE;
  return G0_ENG;
}

ttx_t *ttx_new(unsigned page, const char *lang, long lead_ms, ttx_cb cb, void *ctx) {
  ttx_t *t = calloc(1, sizeof *t);
  if (!t)
    return NULL;
  if (!unham_ready)
    unham_init();
  t->page = page;
  t->magazine = (page / 100) & 0x07; /* 800 -> magazine 0 */
  t->cb = cb;
  t->ctx = ctx;
  t->nat = nat_from_lang(lang);
  t->lead = lead_ms;
  return t;
}

void ttx_free(ttx_t *t) { free(t); }

void ttx_pes(ttx_t *t, int has_pts, uint64_t pts, const unsigned char *d, size_t len) {
  size_t i = 1; /* skip data_identifier */

  if (len < 1 || d[0] < 0x10 || d[0] > 0x1F)
    return;
  if (has_pts)
    t->last_ms = (int64_t)(pts / 90);

  while (i + 2 <= len) {
    unsigned id = d[i], ul = d[i + 1];
    const unsigned char *u = d + i + 2;
    int h0, h1;
    unsigned mag, pkt;

    if (i + 2 + ul > len)
      break;
    if (id == 0x03 && ul >= 44 - 2) { /* EBU teletext subtitle */
      unsigned char b[42];
      int k;
      for (k = 0; k < 42; k++) /* mpag(2) + data(40) */
        b[k] = rev8(u[2 + k]);
      h0 = unham[b[0]];
      h1 = unham[b[1]];
      if (h0 >= 0 && h1 >= 0) {
        mag = (unsigned)h0 & 0x07;
        pkt = ((unsigned)h1 << 1) | (((unsigned)h0 >> 3) & 1);
        if (mag == t->magazine)
          handle_packet(t, mag, pkt, b + 2, t->last_ms);
      }
    }
    i += 2 + ul;
  }
}

void ttx_flush(ttx_t *t) {
  int64_t end;

  if (t->nrows)
    group_done(t);
  end = t->last_ms; /* nothing follows: hold for the minimum */
  if (end < t->cur_start + TTX_MIN_MS)
    end = t->cur_start + TTX_MIN_MS;
  emit_cue(t, end);
  t->have_cur = 0;
}
