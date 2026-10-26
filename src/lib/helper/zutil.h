/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HELPER_ZUTIL_H
#define DVBIPITOOLS_LIB_HELPER_ZUTIL_H

#include <stddef.h>
#include <zlib.h>

static inline int zinflate(const unsigned char *in, size_t inlen, unsigned char *out, size_t *outlen) {
  uLongf n = (uLongf)*outlen;
  if (uncompress(out, &n, in, (uLong)inlen) != Z_OK) return -1;
  *outlen = (size_t)n;
  return 0;
}

static inline int zdeflate(const unsigned char *in, size_t inlen, unsigned char *out, size_t *outlen) {
  uLongf n = (uLongf)*outlen;
  if (compress2(out, &n, in, (uLong)inlen, Z_BEST_COMPRESSION) != Z_OK) return -1;
  *outlen = (size_t)n;
  return 0;
}

#endif
