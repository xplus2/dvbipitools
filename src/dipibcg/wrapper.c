/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "wrapper.h"

static void put24(unsigned char *p, size_t v) {
  p[0] = (unsigned char)(v >> 16);
  p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)v;
}

static size_t get24(const unsigned char *p) {
  return ((size_t)p[0] << 16) | ((size_t)p[1] << 8) | (size_t)p[2];
}

static int build_uncompressed(const unsigned char *container, size_t container_len, unsigned char **out, size_t *out_len) {
  unsigned char *buf = malloc(1 + container_len);
  if (!buf)
    return -1;
  buf[0] = WRAPPER_METHOD_NONE;
  memcpy(buf + 1, container, container_len);
  *out = buf;
  *out_len = 1 + container_len;
  return 0;
}

int wrapper_build(const unsigned char *container, size_t container_len, int want_compress, unsigned char **out, size_t *out_len) {
  uLong bound;
  uLongf compressed_len;
  unsigned char *compbuf, *buf;

  if (!want_compress)
    return build_uncompressed(container, container_len, out, out_len);

  bound = compressBound((uLong)container_len);
  compbuf = malloc(bound);
  if (!compbuf)
    return -1;
  compressed_len = bound;
  if (compress2(compbuf, &compressed_len, container, (uLong)container_len, Z_DEFAULT_COMPRESSION) != Z_OK || compressed_len > 0xFFFFFF) {
    free(compbuf);
    return build_uncompressed(container, container_len, out, out_len);
  }

  buf = malloc(4 + (size_t)compressed_len);
  if (!buf) {
    free(compbuf);
    return -1;
  }
  buf[0] = WRAPPER_METHOD_ZLIB;
  put24(buf + 1, container_len);
  memcpy(buf + 4, compbuf, compressed_len);
  free(compbuf);

  *out = buf;
  *out_len = 4 + (size_t)compressed_len;
  return 0;
}

int wrapper_parse(const unsigned char *data, size_t len, unsigned char **out, size_t *out_len) {
  unsigned method;
  size_t body_len;
  unsigned char *buf;

  if (len < 1)
    return -1;
  method = data[0];

  if (method == WRAPPER_METHOD_NONE) {
    body_len = len - 1;
    buf = malloc(body_len ? body_len : 1);
    if (!buf)
      return -1;
    memcpy(buf, data + 1, body_len);
    *out = buf;
    *out_len = body_len;
    return 0;
  }

  if (method == WRAPPER_METHOD_ZLIB) {
    size_t original_size;
    uLongf dest_len;
    if (len < 4)
      return -1;
    original_size = get24(data + 1);
    buf = malloc(original_size ? original_size : 1);
    if (!buf)
      return -1;
    dest_len = (uLongf)original_size;
    if (uncompress(buf, &dest_len, data + 4, (uLong)(len - 4)) != Z_OK || dest_len != original_size) {
      free(buf);
      return -1;
    }
    *out = buf;
    *out_len = dest_len;
    return 0;
  }

  return -1; /* reserved / user private, not supported */
}
