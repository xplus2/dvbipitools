/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HELPER_CHANKEY_H
#define DVBIPITOOLS_LIB_HELPER_CHANKEY_H

#include <stddef.h>
#include <stdint.h>

static inline uint64_t chan_key_fnv(int family, const void *addr, size_t addr_len, unsigned port) {
  uint64_t h = 1469598103934665603ULL;
  const unsigned char *p = (const unsigned char *)addr;
  for (size_t i = 0; i < addr_len; i++) {
    h ^= p[i];
    h *= 1099511628211ULL;
  }
  h ^= (unsigned)family;
  h *= 1099511628211ULL;
  h ^= port;
  h *= 1099511628211ULL;
  return h;
}

#endif
