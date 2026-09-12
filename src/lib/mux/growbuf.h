/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_GROWBUF_H
#define DVBIPITOOLS_LIB_MUX_GROWBUF_H

#include <stddef.h>

typedef struct {
  unsigned char *p;
  size_t len, cap;
  int err; /* alloc failed */
} muxbuf_t;

void muxbuf_free(muxbuf_t *b);
void muxbuf_append(muxbuf_t *b, const void *data, size_t n, size_t initial_cap);

#endif
