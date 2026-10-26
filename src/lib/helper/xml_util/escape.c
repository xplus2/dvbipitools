/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>

#include "lib/helper/xml_util.h"
#include "lib/helper/xml_util/xml_util_int.h"

int utf8_encode(unsigned long cp, char *out) {
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

static size_t utf8_xml_len(const unsigned char *s) {
  unsigned c = s[0];
  unsigned cp;
  size_t n;
  if (c >= 0xC2 && c <= 0xDF) {
    n = 2;
    cp = c & 0x1F;
  } else if (c >= 0xE0 && c <= 0xEF) {
    n = 3;
    cp = c & 0x0F;
  } else if (c >= 0xF0 && c <= 0xF4) {
    n = 4;
    cp = c & 0x07;
  } else {
    return 0;
  }
  for (size_t i = 1; i < n; i++) {
    if ((s[i] & 0xC0) != 0x80) return 0;
    cp = (cp << 6) | (s[i] & 0x3F);
  }
  if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF))) return 0; /* overlong, out of range */
  if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
  return n;
}

/* plain text passed on in runs */
void xml_escape_to(const char *s, xml_put_fn put, void *ctx) {
  const char *run = s;
  while (*s) {
    unsigned char c = (unsigned char)*s;
    const char *rep = NULL;
    size_t n = 1;
    switch (c) {
    case '&': rep = "&amp;"; break;
    case '<': rep = "&lt;"; break;
    case '>': rep = "&gt;"; break;
    case '"': rep = "&quot;"; break;
    case '\'': rep = "&apos;"; break;
    default:
      if (c >= 0x80) {
        n = utf8_xml_len((const unsigned char *)s);
        if (!n) {
          rep = "\xEF\xBF\xBD";
          n = 1;
        } else if (n == 3 && c == 0xEF && (unsigned char)s[1] == 0xBF && ((unsigned char)s[2] == 0xBE || (unsigned char)s[2] == 0xBF)) {
          rep = "";
        }
      } else if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
        rep = "";
      }
      break;
    }
    if (rep) {
      if (s > run) put(ctx, run, (size_t)(s - run));
      if (*rep) put(ctx, rep, strlen(rep));
      run = s + n;
    }
    s += n;
  }
  if (s > run) put(ctx, run, (size_t)(s - run));
}

static void file_put(void *ctx, const char *s, size_t n) { fwrite(s, 1, n, ctx); }

void xml_escape(FILE *f, const char *s) { xml_escape_to(s, file_put, f); }
