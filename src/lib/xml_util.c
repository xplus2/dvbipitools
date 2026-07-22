/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "xml_util.h"

static int utf8_encode(unsigned long cp, char *out) {
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

void xml_escape(FILE *f, const char *s) {
  for (; *s; s++) {
    switch (*s) {
    case '&': fputs("&amp;", f); break;
    case '<': fputs("&lt;", f); break;
    case '>': fputs("&gt;", f); break;
    case '"': fputs("&quot;", f); break;
    case '\'': fputs("&apos;", f); break;
    default: fputc(*s, f); break;
    }
  }
}

static void decode_copy(const char *src, size_t n, char *out, size_t outcap) {
  size_t i, oi = 0;
  for (i = 0; i < n && oi + 1 < outcap;) {
    if (!strncmp(src + i, "&amp;", 5)) { out[oi++] = '&'; i += 5; }
    else if (!strncmp(src + i, "&lt;", 4)) { out[oi++] = '<'; i += 4; }
    else if (!strncmp(src + i, "&gt;", 4)) { out[oi++] = '>'; i += 4; }
    else if (!strncmp(src + i, "&quot;", 6)) { out[oi++] = '"'; i += 6; }
    else if (!strncmp(src + i, "&apos;", 6)) { out[oi++] = '\''; i += 6; }
    else if (src[i] == '&' && i + 2 < n && src[i + 1] == '#') {
      size_t j = i + 2;
      int hex = 0;
      unsigned long cp;
      char *endp;
      if (j < n && (src[j] == 'x' || src[j] == 'X')) { hex = 1; j++; }
      cp = strtoul(src + j, &endp, hex ? 16 : 10);
      if (endp != src + j && *endp == ';' && (size_t)(endp - src) < n) {
        char utf8[4];
        int len = utf8_encode(cp, utf8);
        if (oi + (size_t)len + 1 > outcap)
          break;
        memcpy(out + oi, utf8, (size_t)len);
        oi += (size_t)len;
        i = (size_t)(endp - src) + 1;
      } else {
        out[oi++] = src[i];
        i++;
      }
    }
    else { out[oi++] = src[i]; i++; }
  }
  out[oi] = '\0';
}

int xml_elem_text(const char *s, const char *end, const char *tag, char *out, size_t outcap) {
  size_t taglen = strlen(tag);
  const char *p = s, *gt, *close;
  char closetag[64];

  for (;;) {
    const char *hit = strstr(p, tag);
    if (!hit || hit >= end)
      return -1;
    if (hit > s && hit[-1] == '<' && (hit[taglen] == '>' || hit[taglen] == ' ' || hit[taglen] == '\t' || hit[taglen] == '/')) {
      p = hit;
      break;
    }
    p = hit + 1;
  }
  gt = strchr(p, '>');
  if (!gt || gt >= end)
    return -1;
  if (gt[-1] == '/')
    return -1; /* self-closing, no text */
  snprintf(closetag, sizeof closetag, "</%s>", tag);
  close = strstr(gt + 1, closetag);
  if (!close || close > end)
    return -1;
  decode_copy(gt + 1, (size_t)(close - (gt + 1)), out, outcap);
  return 0;
}

int xml_attr(const char *s, const char *end, const char *name, char *out, size_t outcap) {
  size_t namelen = strlen(name);
  const char *p = s;
  while (p < end) {
    const char *hit = strstr(p, name);
    const char *v, *q;
    if (!hit || hit >= end)
      return -1;
    if (hit[namelen] != '=' || hit[namelen + 1] != '"') {
      p = hit + 1;
      continue;
    }
    v = hit + namelen + 2;
    q = strchr(v, '"');
    if (!q || q > end)
      return -1;
    decode_copy(v, (size_t)(q - v), out, outcap);
    return 0;
  }
  return -1;
}
