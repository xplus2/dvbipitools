/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/helper/beutil.h"
#include "biss.h"

static int parse_hex(const char *hex, unsigned char *out, size_t len) {
  if (!hex || !out)
    return -1;
  if ((hex[0] == '0') && (hex[1] == 'x' || hex[1] == 'X'))
    hex += 2;
  if (strlen(hex) != len * 2)
    return -1;
  for (size_t i = 0; i < len; i++) {
    int hi = hex_nibble(hex[2 * i]);
    int lo = hex_nibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0)
      return -1;
    out[i] = (unsigned char)((hi << 4) | lo);
  }
  return 0;
}

int biss_parse_hex16(const char *hex, unsigned char out[BISS_KEY_LEN]) { return parse_hex(hex, out, BISS_KEY_LEN); }

int biss1_parse_id(const char *hex, unsigned char out[BISS1_ID_LEN]) { return parse_hex(hex, out, BISS1_ID_LEN); }

int biss1_parse_esw(const char *hex, unsigned char out[BISS1_ESW_LEN]) { return parse_hex(hex, out, BISS1_ESW_LEN); }

void biss1_sw_to_cw(const unsigned char sw[BISS1_SW_LEN], unsigned char cw_out[BISS1_KEY_LEN]) {
  for (size_t i = 0; i < BISS1_SW_LEN; i++) cw_out[i < 3 ? i : i + 1] = sw[i];
  cw_out[3] = (unsigned char)(((unsigned)sw[0] + sw[1] + sw[2]) % 256);
  cw_out[7] = (unsigned char)(((unsigned)sw[3] + sw[4] + sw[5]) % 256);
}

int biss1_parse_sw(const char *hex, unsigned char cw_out[BISS1_KEY_LEN]) {
  unsigned char sw[BISS1_SW_LEN];
  if (!cw_out || parse_hex(hex, sw, BISS1_SW_LEN))
    return -1;
  biss1_sw_to_cw(sw, cw_out);
  return 0;
}
