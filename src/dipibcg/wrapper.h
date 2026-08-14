/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBCG_WRAPPER_H
#define DIPIBCG_WRAPPER_H

#include <stddef.h>

/* TS 102 323 7.3.1.5 table 22 */
#define WRAPPER_METHOD_NONE 0x00
#define WRAPPER_METHOD_ZLIB 0x01

/* wraps container (TS 102 822-3-2 4.5.2.1) in a compression_wrapper (TS 102 323 7.3.1.5).
   want_compress requests zlib (RFC 1950); fallback to uncompressed w/o zlib or if compression fails.
   *out is malloc'd, caller frees. 0 ok, -1 oom */
int wrapper_build(const unsigned char *container, size_t container_len, int want_compress, unsigned char **out, size_t *out_len);

/* reverses wrapper_build. *out is malloc'd regardless of method, caller frees.
   0 ok, -1 bad input or (build w/o zlib) a zlib-compressed wrapper */
int wrapper_parse(const unsigned char *data, size_t len, unsigned char **out, size_t *out_len);

#endif
