/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "base64.h"
#include <stdint.h>

static const char b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t base64_encoded_len(size_t len) { return ((len + 2) / 3) * 4; }

void base64_encode(const void *data, size_t len, char *out) {
  const unsigned char *p = data;
  size_t i, o = 0;

  for (i = 0; i + 3 <= len; i += 3) {
    uint_least32_t v = ((uint_least32_t)p[i] << 16) | ((uint_least32_t)p[i + 1] << 8) | p[i + 2];
    out[o++] = b64_alphabet[(v >> 18) & 0x3f];
    out[o++] = b64_alphabet[(v >> 12) & 0x3f];
    out[o++] = b64_alphabet[(v >> 6) & 0x3f];
    out[o++] = b64_alphabet[v & 0x3f];
  }
  if (len - i == 1) {
    uint_least32_t v = (uint_least32_t)p[i] << 16;
    out[o++] = b64_alphabet[(v >> 18) & 0x3f];
    out[o++] = b64_alphabet[(v >> 12) & 0x3f];
    out[o++] = '=';
    out[o++] = '=';
  } else if (len - i == 2) {
    uint_least32_t v = ((uint_least32_t)p[i] << 16) | ((uint_least32_t)p[i + 1] << 8);
    out[o++] = b64_alphabet[(v >> 18) & 0x3f];
    out[o++] = b64_alphabet[(v >> 12) & 0x3f];
    out[o++] = b64_alphabet[(v >> 6) & 0x3f];
    out[o++] = '=';
  }
  out[o] = '\0';
}
