/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <iconv.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/xml_util.h"

static const char *find_bytes(const char *s, const char *end, const char *pat) {
  size_t n = strlen(pat);
  for (; (size_t)(end - s) >= n; s++)
    if (!memcmp(s, pat, n)) return s;
  return NULL;
}

/* encoding="x" value of XML declaration at start of [s,end) into out. 0 found, -1 none */
static int decl_encoding(const char *s, const char *end, char *out, size_t cap) {
  const char *decl_end;
  const char *p;
  char q;
  size_t n = 0;
  if (end - s >= 3 && !memcmp(s, "\xEF\xBB\xBF", 3)) s += 3;
  if (end - s < 6 || memcmp(s, "<?xml", 5) || !isspace((unsigned char)s[5])) return -1;
  decl_end = find_bytes(s, end, "?>");
  if (!decl_end) return -1;
  p = find_bytes(s, decl_end, "encoding");
  if (!p) return -1;
  p += 8;
  while (p < decl_end && isspace((unsigned char)*p)) p++;
  if (p >= decl_end || *p != '=') return -1;
  p++;
  while (p < decl_end && isspace((unsigned char)*p)) p++;
  if (p >= decl_end || (*p != '"' && *p != '\'')) return -1;
  q = *p++;
  while (p < decl_end && *p != q && n + 1 < cap) out[n++] = *p++;
  if (p >= decl_end || *p != q) return -1;
  out[n] = '\0';
  return n ? 0 : -1;
}

static int is_utf8_name(const char *enc) {
  return !strcasecmp(enc, "UTF-8") || !strcasecmp(enc, "UTF8") || !strcasecmp(enc, "US-ASCII") || !strcasecmp(enc, "ASCII");
}

static int convert_to_utf8(iconv_t cd, char *in, size_t len, char **out, size_t *out_len) {
  size_t cap = len * 2 + 16;
  size_t used = 0;
  char *buf = malloc(cap);
  char *ip = in;
  size_t left = len;
  if (!buf) return -2;
  while (left > 0) {
    char *op = buf + used;
    size_t oleft = cap - used - 1;
    if (iconv(cd, &ip, &left, &op, &oleft) == (size_t)-1 && errno != E2BIG) {
      free(buf);
      return -1;
    }
    used = (size_t)(op - buf);
    if (left > 0) {
      char *nb;
      cap *= 2;
      nb = realloc(buf, cap);
      if (!nb) {
        free(buf);
        return -2;
      }
      buf = nb;
    }
  }
  buf[used] = '\0';
  *out = buf;
  *out_len = used;
  return 0;
}

int xml_to_utf8(char **buf, size_t *len, char *enc, size_t enccap) {
  char name[64];
  iconv_t cd;
  char *out;
  size_t out_len;
  int rc;
  if (enccap) enc[0] = '\0';
  if (decl_encoding(*buf, *buf + *len, name, sizeof name)) return 0;
  if (enccap) {
    strncpy(enc, name, enccap - 1);
    enc[enccap - 1] = '\0';
  }
  if (is_utf8_name(name)) return 0;
  cd = iconv_open("UTF-8", name);
  if (cd == (iconv_t)-1) return -1;
  rc = convert_to_utf8(cd, *buf, *len, &out, &out_len);
  iconv_close(cd);
  if (rc) return rc;
  free(*buf);
  *buf = out;
  *len = out_len;
  return 0;
}
