/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HELPER_GROWUTIL_H
#define DVBIPITOOLS_LIB_HELPER_GROWUTIL_H

#include <stddef.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* grow arr for need elems of elemsz B, doubling *cap (16 initial, or need if bigger).
   new arr on success, NULL on OOM (arr, *cap unchanged) */
void *array_grow(void *arr, int *cap, int need, size_t elemsz);
int array_reserve(void **arr, int *cap, int need, size_t elemsz);
int grow_zeroed(void **arr, int *n, int need, size_t elemsz);

/* doubles *cap (elem_size units, initial_elems if 0, 1 if both 0) until >= need_elems, realloc *buf.
   0 ok, -1 OOM or size overflow (*buf, *cap unchanged) */
int growbuf_reserve(void **buf, size_t *cap, size_t elem_size, size_t need_elems, size_t initial_elems);

/* power of two >= n, 1 if n == 0 */
static inline size_t next_pow2(size_t n) {
  size_t p = 1;
  while (p < n) p <<= 1;
  return p;
}

#endif
