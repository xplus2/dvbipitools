/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "handshake.h"

/* not stdlib rand(): avoids reseeding global RNG state on every reconnect */
static uint32_t xorshift32(uint32_t *s) {
  *s ^= *s << 13;
  *s ^= *s >> 17;
  *s ^= *s << 5;
  return *s;
}

void rtmp_handshake_c1(unsigned char *c1, uint32_t timestamp) {
  uint32_t s = timestamp ? timestamp : 1;

  c1[0] = (unsigned char)(timestamp >> 24);
  c1[1] = (unsigned char)(timestamp >> 16);
  c1[2] = (unsigned char)(timestamp >> 8);
  c1[3] = (unsigned char)timestamp;
  memset(c1 + 4, 0, 4);
  for (size_t i = 8; i < RTMP_HANDSHAKE_SIZE; i += 4) {
    uint32_t v = xorshift32(&s);
    c1[i] = (unsigned char)(v >> 24);
    c1[i + 1] = (unsigned char)(v >> 16);
    c1[i + 2] = (unsigned char)(v >> 8);
    c1[i + 3] = (unsigned char)v;
  }
}

void rtmp_handshake_c2(unsigned char *c2, const unsigned char *s1, uint32_t timestamp) {
  memcpy(c2, s1, RTMP_HANDSHAKE_SIZE);
  c2[4] = (unsigned char)(timestamp >> 24);
  c2[5] = (unsigned char)(timestamp >> 16);
  c2[6] = (unsigned char)(timestamp >> 8);
  c2[7] = (unsigned char)timestamp;
}
