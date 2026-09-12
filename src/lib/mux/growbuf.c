/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/helper/ioutil.h"

#include "growbuf.h"

void muxbuf_free(muxbuf_t *b) {
  free(b->p);
  b->p = NULL;
  b->len = b->cap = 0;
}

void muxbuf_append(muxbuf_t *b, const void *data, size_t n, size_t initial_cap) {
  if (b->err) return;
  if (growbuf_reserve((void **)&b->p, &b->cap, 1, b->len + n, initial_cap)) {
    b->err = 1;
    return;
  }
  memcpy(b->p + b->len, data, n);
  b->len += n;
}
