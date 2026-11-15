/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "growutil.h"

void *array_grow(void *arr, int *cap, int need, size_t elemsz) {
  int newcap;
  void *p;
  if (need <= *cap) return arr;
  newcap = *cap ? *cap * 2 : 16;
  if (newcap < need) newcap = need;
  if (elemsz && (size_t)newcap > PTRDIFF_MAX / elemsz) return NULL;
  p = realloc(arr, (size_t)newcap * elemsz);
  if (!p) return NULL;
  *cap = newcap;
  return p;
}

int array_reserve(void **arr, int *cap, int need, size_t elemsz) {
  void *np;
  if (need <= *cap) return 0;
  np = array_grow(*arr, cap, need, elemsz);
  if (!np) return -1;
  *arr = np;
  return 0;
}

int grow_zeroed(void **arr, int *n, int need, size_t elemsz) {
  unsigned char *np;
  if (need <= *n) return 0;
  np = realloc(*arr, (size_t)need * elemsz);
  if (!np) return -1;
  memset(np + (size_t)*n * elemsz, 0, (size_t)(need - *n) * elemsz);
  *arr = np;
  *n = need;
  return 0;
}

int growbuf_reserve(void **buf, size_t *cap, size_t elem_size, size_t need_elems, size_t initial_elems) {
  size_t ncap;
  void *np;
  if (*cap >= need_elems) return 0;
  ncap = *cap ? *cap : initial_elems;
  if (!ncap) ncap = 1;
  while (ncap < need_elems) {
    if (ncap > SIZE_MAX / 2) {
      ncap = need_elems;
      break;
    }
    ncap *= 2;
  }
  if (!elem_size || ncap > PTRDIFF_MAX / elem_size) return -1;
  np = realloc(*buf, ncap * elem_size);
  if (!np) return -1;
  *buf = np;
  *cap = ncap;
  return 0;
}
