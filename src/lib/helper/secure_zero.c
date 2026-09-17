/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _DEFAULT_SOURCE
#define __STDC_WANT_LIB_EXT1__ 1
#include <string.h>

#include "secure_zero.h"

void secure_zero(void *ptr, size_t len) {
#if defined(__STDC_LIB_EXT1__)
  memset_s(ptr, len, 0, len);
#elif defined(__GLIBC__)
  explicit_bzero(ptr, len);
#else
  volatile unsigned char *p = ptr;
  while (len--)
    *p++ = 0;
#endif
}

int secure_eq(const void *a, const void *b, size_t len) {
  const volatile unsigned char *pa = a;
  const volatile unsigned char *pb = b;
  unsigned char diff = 0;
  for (size_t i = 0; i < len; i++) diff |= pa[i] ^ pb[i];
  return diff == 0;
}
