/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/helper/log.h"
#include "wrapper.h"

/* zlib stub: the wrapper envelope still mandatory (TS 102 323 7.3.1.5) */
int wrapper_build(const unsigned char *container, size_t container_len, int want_compress, unsigned char **out, size_t *out_len) {
  unsigned char *buf;
  (void)want_compress;
  buf = malloc(1 + container_len);
  if (!buf)
    return -1;
  buf[0] = WRAPPER_METHOD_NONE;
  memcpy(buf + 1, container, container_len);
  *out = buf;
  *out_len = 1 + container_len;
  return 0;
}

int wrapper_parse(const unsigned char *data, size_t len, unsigned char **out, size_t *out_len) {
  unsigned char *buf;
  size_t body_len;

  if (len < 1)
    return -1;
  if (data[0] != WRAPPER_METHOD_NONE) {
    log_line("dipibcg: no zlib support, cannot decompress received container");
    return -1;
  }
  body_len = len - 1;
  buf = malloc(body_len ? body_len : 1);
  if (!buf)
    return -1;
  memcpy(buf, data + 1, body_len);
  *out = buf;
  *out_len = body_len;
  return 0;
}
