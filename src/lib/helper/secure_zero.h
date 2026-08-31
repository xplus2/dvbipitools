/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_SECURE_ZERO_H
#define LIB_SECURE_ZERO_H

#include <stddef.h>

/* zeroes len bytes at ptr. survives compiler dead-store elimination
   (memset can get optimized out when the buffer isn't read again) */
void secure_zero(void *ptr, size_t len);

#endif
