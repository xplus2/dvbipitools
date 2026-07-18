/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "crc32.h"

uint32_t crc32_mpeg(const unsigned char *data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  size_t i;
  int b;
  for (i = 0; i < len; i++) {
    crc ^= (uint32_t)data[i] << 24;
    for (b = 0; b < 8; b++)
      crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : (crc << 1);
  }
  return crc;
}
