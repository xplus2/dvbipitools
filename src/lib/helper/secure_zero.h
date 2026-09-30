/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HELPER_SECURE_ZERO_H
#define DVBIPITOOLS_LIB_HELPER_SECURE_ZERO_H

#include <stddef.h>

/* zeroes len bytes at ptr. survives compiler dead-store elimination
   (memset can get optimized out when the buffer isn't read again) */
void secure_zero(void *ptr, size_t len);

/* constant time comparison */
int secure_eq(const void *a, const void *b, size_t len);

#endif
