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

void xml_escape(FILE *f, const char *s) {
  while (*s) {
    unsigned char c = (unsigned char)*s;
    switch (c) {
    case '&': fputs("&amp;", f); break;
    case '<': fputs("&lt;", f); break;
    case '>': fputs("&gt;", f); break;
    case '"': fputs("&quot;", f); break;
    case '\'': fputs("&apos;", f); break;
    default:
      if (c >= 0x80) {
        size_t n = utf8_xml_len((const unsigned char *)s);
        if (!n) {
          fputs("\xEF\xBF\xBD", f);
          s++;
          continue;
        }
        if (!(n == 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBF && ((unsigned char)s[2] == 0xBE || (unsigned char)s[2] == 0xBF))) fwrite(s, 1, n, f);
        s += n;
        continue;
      }
      if (c >= 0x20 || c == '\t' || c == '\n' || c == '\r') fputc(c, f);
      break;
    }
    s++;
  }
}
