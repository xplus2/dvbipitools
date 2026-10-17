/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <iconv.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/xml_util.h"
#include "lib/helper/xml_util/xml_util_int.h"

void sink_put(sink_t *k, const char *src, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (k->len + 1 >= k->cap) {
      k->cut = 1;
      return;
    }
    k->out[k->len++] = src[i];
  }
}

static const struct {
  const char *ent;
  size_t len;
  char ch;
} entities[] = {
    {"&amp;", 5, '&'}, {"&lt;", 4, '<'}, {"&gt;", 4, '>'}, {"&quot;", 6, '"'}, {"&apos;", 6, '\''},
};

/* &#NNN; or &#xHH; at src. consumed length, 0 if invalid */
static size_t numeric_ref(const char *src, size_t n, sink_t *k) {
  size_t j = 2;
  int hex = 0;
  unsigned long cp;
  char *endp;
  char utf8[4];
  int len;

  if (j < n && (src[j] == 'x' || src[j] == 'X')) {
    hex = 1;
    j++;
  }
  if (j >= n || !isxdigit((unsigned char)src[j])) return 0;
  cp = strtoul(src + j, &endp, hex ? 16 : 10);
  if ((size_t)(endp - src) >= n || *endp != ';') return 0;
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) || cp == 0xFFFE || cp == 0xFFFF) return 0;
  if (cp < 0x20 && cp != 0x9 && cp != 0xA && cp != 0xD) return 0;
  len = utf8_encode(cp, utf8);
  sink_put(k, utf8, (size_t)len);
  return (size_t)(endp - src) + 1;
}

void sink_decode(sink_t *k, const char *src, size_t n) {
  size_t i = 0;
  while (i < n && !k->cut) {
    size_t used = 0;
    if (src[i] == '&') {
      for (size_t e = 0; e < sizeof entities / sizeof entities[0] && !used; e++) {
        if (n - i >= entities[e].len && !memcmp(src + i, entities[e].ent, entities[e].len)) {
          sink_put(k, &entities[e].ch, 1);
          used = entities[e].len;
        }
      }
      if (!used && n - i > 2 && src[i + 1] == '#') used = numeric_ref(src + i, n - i, k);
    }
    if (!used) {
      sink_put(k, src + i, 1);
      used = 1;
    }
    i += used;
  }
}

/* len minus a trailing incomplete UTF-8 sequence */
static size_t utf8_complete_len(const char *s, size_t len) {
  size_t i = len;
  size_t need;
  unsigned char lead;
  while (i > 0 && len - i < 3 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) i--;
  if (i == 0) return len;
  lead = (unsigned char)s[i - 1];
  if (lead >= 0xF0) need = 4;
  else if (lead >= 0xE0) need = 3;
  else if (lead >= 0xC0) need = 2;
  else need = 1;
  return need > len - (i - 1) ? i - 1 : len;
}

void sink_init(sink_t *k, char *out, size_t cap) {
  k->out = out;
  k->cap = cap;
  k->len = 0;
  k->cut = 0;
}

/* NUL-terminates, trims a cut UTF-8 sequence. returns cut flag */
int sink_finish(sink_t *k, int *truncated) {
  if (k->cut) k->len = utf8_complete_len(k->out, k->len);
  k->out[k->len] = '\0';
  if (truncated) *truncated = k->cut;
  return k->cut;
}
