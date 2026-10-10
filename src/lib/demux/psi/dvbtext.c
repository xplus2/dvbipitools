/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>

#include "dvbtext.h"
#include "dvbtext_tab.h"

#define KSC_TRAIL_COUNT 94
#define BIG5_TRAIL_COUNT 157
#define BIG5_TRAIL_LOW_COUNT 63
#define BIG5_LEAD_MAX 0xF9
#define DBCS_LEAD_MIN 0xA1
#define DBCS_LEAD_MAX 0xFE
#define CP_BAD 0xFFFFFFFFu

typedef enum { TXT_6937, TXT_8859, TXT_UCS2, TXT_UTF8, TXT_KSC, TXT_GB, TXT_BIG5 } txt_kind_t;

typedef struct {
  char *dst;
  size_t cap;
  size_t o;
  int full;
} sink_t;

static void put_cp(sink_t *w, unsigned cp) {
  unsigned char u[4];
  size_t n;
  if (cp == CP_BAD) cp = '?';
  else if (cp < 0x20) cp = ' ';
  else if (cp == 0x7F) return;
  else if ((cp >= 0x80 && cp <= 0x9F) || (cp >= 0xE080 && cp <= 0xE09F)) {
    if ((cp & 0xFF) != 0x8A) return; /* CR/LF -> space, rest incl emphasis dropped */
    cp = ' ';
  }
  if (cp < 0x80) {
    u[0] = (unsigned char)cp;
    n = 1;
  } else if (cp < 0x800) {
    u[0] = (unsigned char)(0xC0 | (cp >> 6));
    u[1] = (unsigned char)(0x80 | (cp & 0x3F));
    n = 2;
  } else if (cp < 0x10000) {
    u[0] = (unsigned char)(0xE0 | (cp >> 12));
    u[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    u[2] = (unsigned char)(0x80 | (cp & 0x3F));
    n = 3;
  } else {
    u[0] = (unsigned char)(0xF0 | (cp >> 18));
    u[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    u[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    u[3] = (unsigned char)(0x80 | (cp & 0x3F));
    n = 4;
  }
  if (w->full || w->o + n + 1 > w->cap) {
    w->full = 1;
    return;
  }
  for (size_t k = 0; k < n; k++) w->dst[w->o++] = (char)u[k];
}

static unsigned mapped(unsigned v) { return v ? v : CP_BAD; }

static unsigned pair_lookup(unsigned lead, unsigned trail) {
  size_t lo = 0;
  size_t hi = sizeof t6937_pairs / sizeof t6937_pairs[0];
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    const dvbtext_pair_t *p = &t6937_pairs[mid];
    if (p->lead == lead && p->trail == trail) return p->cp;
    if (p->lead < lead || (p->lead == lead && p->trail < trail)) lo = mid + 1;
    else hi = mid;
  }
  return 0;
}

static void conv_6937(sink_t *w, const unsigned char *s, size_t len) {
  for (size_t i = 0; i < len; i++) {
    unsigned b = s[i];
    if (b < 0xA0) {
      put_cp(w, b);
    } else if (b >= 0xC1 && b <= 0xCF && t6937_spacing[b - 0xC0]) {
      unsigned cp;
      if (i + 1 >= len) break;
      if (s[i + 1] == ' ') {
        put_cp(w, t6937_spacing[b - 0xC0]);
        i++;
      } else if ((cp = pair_lookup(b, s[i + 1])) != 0) {
        put_cp(w, cp);
        i++;
      }
    } else {
      put_cp(w, mapped(t6937[b - 0xA0]));
    }
  }
}

static void conv_8859(sink_t *w, const unsigned char *s, size_t len, const uint16_t *tab) {
  for (size_t i = 0; i < len; i++) {
    unsigned b = s[i];
    if (b < 0xA0) put_cp(w, b);
    else put_cp(w, tab ? mapped(tab[b - 0xA0]) : CP_BAD);
  }
}

static void conv_ucs2(sink_t *w, const unsigned char *s, size_t len) {
  for (size_t i = 0; i + 1 < len; i += 2) {
    unsigned cp = ((unsigned)s[i] << 8) | s[i + 1];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < len) {
      unsigned lo = ((unsigned)s[i + 2] << 8) | s[i + 3];
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        i += 2;
      }
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) cp = CP_BAD;
    put_cp(w, cp);
  }
}

static size_t utf8_seq(const unsigned char *s, size_t len, unsigned *cp) {
  unsigned b = s[0];
  size_t n;
  unsigned v;
  unsigned min;
  if (b < 0x80) {
    *cp = b;
    return 1;
  }
  if (b >= 0xC2 && b <= 0xDF) {
    n = 2;
    v = b & 0x1F;
    min = 0x80;
  } else if (b >= 0xE0 && b <= 0xEF) {
    n = 3;
    v = b & 0x0F;
    min = 0x800;
  } else if (b >= 0xF0 && b <= 0xF4) {
    n = 4;
    v = b & 0x07;
    min = 0x10000;
  } else {
    return 0;
  }
  if (len < n) return 0;
  for (size_t k = 1; k < n; k++) {
    if ((s[k] & 0xC0) != 0x80) return 0;
    v = (v << 6) | (s[k] & 0x3F);
  }
  if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return 0;
  *cp = v;
  return n;
}

static void conv_utf8(sink_t *w, const unsigned char *s, size_t len) {
  for (size_t i = 0; i < len;) {
    unsigned cp = 0;
    size_t n = utf8_seq(s + i, len - i, &cp);
    if (!n) {
      put_cp(w, CP_BAD);
      i++;
    } else {
      put_cp(w, cp);
      i += n;
    }
  }
}

static void conv_dbcs(sink_t *w, const unsigned char *s, size_t len, txt_kind_t kind) {
  for (size_t i = 0; i < len; i++) {
    unsigned b = s[i];
    unsigned t;
    unsigned cp = 0;
    if (b < DBCS_LEAD_MIN) {
      put_cp(w, b);
      continue;
    }
    if (b > DBCS_LEAD_MAX || (kind == TXT_BIG5 && b > BIG5_LEAD_MAX) || i + 1 >= len) {
      put_cp(w, CP_BAD);
      continue;
    }
    t = s[i + 1];
    if (kind == TXT_BIG5) {
      if (t >= 0x40 && t <= 0x7E) cp = t_big5[(b - DBCS_LEAD_MIN) * BIG5_TRAIL_COUNT + (t - 0x40)];
      else if (t >= DBCS_LEAD_MIN && t <= DBCS_LEAD_MAX) cp = t_big5[(b - DBCS_LEAD_MIN) * BIG5_TRAIL_COUNT + BIG5_TRAIL_LOW_COUNT + (t - DBCS_LEAD_MIN)];
      else t = 0;
    } else if (t >= DBCS_LEAD_MIN && t <= DBCS_LEAD_MAX) {
      const uint16_t *tab = (kind == TXT_KSC) ? t_ksx1001 : t_gb2312;
      cp = tab[(b - DBCS_LEAD_MIN) * KSC_TRAIL_COUNT + (t - DBCS_LEAD_MIN)];
    } else {
      t = 0;
    }
    put_cp(w, mapped(cp));
    if (t) i++; /* bad trail: resync on it */
  }
}

size_t dvbtext_to_utf8(char *dst, size_t dstsz, const unsigned char *src, size_t len) {
  sink_t w;
  txt_kind_t kind = TXT_6937;
  const uint16_t *tab = NULL;
  size_t i = 0;
  if (!dstsz) return 0;
  w.dst = dst;
  w.cap = dstsz;
  w.o = 0;
  w.full = 0;
  if (len && src[0] < 0x20) {
    unsigned b = src[0];
    i = 1;
    if (b >= 0x01 && b <= 0x0B) {
      kind = TXT_8859;
      tab = t8859[b + 4]; /* 0x01..0x0B = 8859-5..15 */
    } else if (b == 0x10 && len >= 3) {
      kind = TXT_8859;
      if (src[1] == 0 && src[2] <= 15) tab = t8859[src[2]];
      i = 3;
    } else if (b == 0x11) {
      kind = TXT_UCS2;
    } else if (b == 0x12) {
      kind = TXT_KSC;
    } else if (b == 0x13) {
      kind = TXT_GB;
    } else if (b == 0x14) {
      kind = TXT_BIG5;
    } else if (b == 0x15) {
      kind = TXT_UTF8;
    } else if (b == 0x1F && len >= 2) {
      kind = TXT_8859; /* BOCU-1/SCSU not decoded: ASCII only */
      i = 2;
    }
  }
  switch (kind) {
    case TXT_6937: conv_6937(&w, src + i, len - i); break;
    case TXT_8859: conv_8859(&w, src + i, len - i, tab); break;
    case TXT_UCS2: conv_ucs2(&w, src + i, len - i); break;
    case TXT_UTF8: conv_utf8(&w, src + i, len - i); break;
    case TXT_KSC:
    case TXT_GB:
    case TXT_BIG5: conv_dbcs(&w, src + i, len - i, kind); break;
  }
  dst[w.o] = '\0';
  return w.o;
}
