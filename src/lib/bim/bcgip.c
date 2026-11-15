/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/bcgip.h"
#include "lib/helper/beutil.h"

#define METHOD_NONE 0x00
#define METHOD_ZLIB 0x01
#define UINT24_MAX 0xFFFFFFu

static int fail(bim_err_t *err, const char *what) { return bim_fail(err, BIM_BCG_ERR, "bim: BCG %s", what); }

static int plain(const unsigned char *c, size_t len, unsigned char **out, size_t *out_len) {
  unsigned char *b = malloc(len + 1);
  if (!b) return -1;
  b[0] = METHOD_NONE;
  if (len) memcpy(b + 1, c, len);
  *out = b;
  *out_len = len + 1;
  return 0;
}

int bim_bcg_wrap(const unsigned char *container, size_t len, bim_deflate_fn deflate, unsigned char **out, size_t *out_len) {
  size_t cap = len + (len >> 8) + 64;
  unsigned char *b;
  size_t n = cap;
  if (!deflate) return plain(container, len, out, out_len);
  if (len > UINT24_MAX) return plain(container, len, out, out_len);
  b = malloc(4 + cap);
  if (!b) return plain(container, len, out, out_len);
  if (deflate(container, len, b + 4, &n) || n > UINT24_MAX) {
    free(b);
    return plain(container, len, out, out_len);
  }
  b[0] = METHOD_ZLIB;
  be24_put(b + 1, (uint32_t)len);
  *out = b;
  *out_len = 4 + n;
  return 0;
}

int bim_bcg_unwrap(const unsigned char *data, size_t len, bim_inflate_fn inflate, unsigned char **out, size_t *out_len) {
  unsigned char *b;
  size_t n;
  if (len < 1) return -1;
  if (data[0] == METHOD_NONE) {
    if (!(b = malloc(len))) return -1;
    memcpy(b, data + 1, len - 1);
    *out = b;
    *out_len = len - 1;
    return 0;
  }
  if (data[0] != METHOD_ZLIB || len < 4 || !inflate) return -1;
  n = be24_get(data + 1);
  if (!(b = malloc(n ? n : 1))) return -1;
  if (inflate(data + 4, len - 4, b, &n) || n != be24_get(data + 1)) {
    free(b);
    return -1;
  }
  *out = b;
  *out_len = n;
  return 0;
}

int bim_bcg_tvainit_write(const bim_tvainit_t *i, const bim_decoderinit_t *di, bitwriter_t *bw, bim_err_t *err) {
  if (i->encoding_version != BIM_TVAINIT_DVB) return fail(err, "DVB-TVA-init needs EncodingVersion 0xF0");
  return bim_tvainit_write(i, bw, err) || bim_decoderinit_write(di, NULL, NULL, bw, err) ? -1 : 0;
}

int bim_bcg_tvainit_parse(const unsigned char *data, size_t len, bim_tvainit_t *i, bim_decoderinit_t *di, bim_err_t *err) {
  size_t ptr;
  size_t used;
  if (bim_tvainit_parse(data, len, i, &ptr, err)) return -1;
  if (i->encoding_version != BIM_TVAINIT_DVB) return fail(err, "DVB-TVA-init needs EncodingVersion 0xF0");
  if (bim_decoderinit_parse(data + ptr, len - ptr, 0, NULL, NULL, di, &used, err)) return -1;
  if (ptr + used != len) {
    bim_decoderinit_free(di);
    return fail(err, "DVB-TVA-init has trailing bytes");
  }
  return 0;
}
