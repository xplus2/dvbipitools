/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BEUTIL_H
#define DVBIPITOOLS_LIB_BEUTIL_H

#include <stdint.h>

static inline void be16_put(unsigned char *p, uint16_t v) {
  p[0] = (unsigned char)(v >> 8);
  p[1] = (unsigned char)v;
}

static inline void be24_put(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)(v >> 16);
  p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)v;
}

static inline void be32_put(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}

static inline void be64_put(unsigned char *p, uint64_t v) {
  be32_put(p, (uint32_t)(v >> 32));
  be32_put(p + 4, (uint32_t)v);
}

static inline uint16_t be16_get(const unsigned char *p) {
  return (uint16_t)(((unsigned)p[0] << 8) | p[1]);
}

static inline uint32_t be24_get(const unsigned char *p) {
  return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static inline uint32_t be32_get(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static inline uint64_t be64_get(const unsigned char *p) {
  return ((uint64_t)be32_get(p) << 32) | be32_get(p + 4);
}

#endif
