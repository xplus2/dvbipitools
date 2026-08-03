/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* AFL++ harness: feeds file argv[1] as the payload of an ECMG channel_status message
 * (TS 103 197 clause 5.4.2) into ecmg_parse_channel_status(). Bytes straight off the
 * ECMG's TCP connection. Build with afl-cc/afl-clang-fast for instrumentation. */

#include <stdio.h>
#include <stdlib.h>

#include "dipitvhead/cas/ecmg_client.h"

int main(int argc, char **argv) {
  FILE *f;
  unsigned char *buf;
  long len;
  size_t n;
  unsigned lead_cw, cw_per_msg, max_comp_time_ms, min_cp_100ms, ecm_rep_period_ms;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <input-file>\n", argv[0]);
    return 1;
  }
  f = fopen(argv[1], "rb");
  if (!f)
    return 1;
  if (fseek(f, 0, SEEK_END)) {
    fclose(f);
    return 1;
  }
  len = ftell(f);
  if (len < 0 || fseek(f, 0, SEEK_SET)) {
    fclose(f);
    return 1;
  }
  buf = malloc((size_t)len ? (size_t)len : 1);
  if (!buf) {
    fclose(f);
    return 1;
  }
  n = fread(buf, 1, (size_t)len, f);
  fclose(f);
  ecmg_parse_channel_status(buf, n, &lead_cw, &cw_per_msg, &max_comp_time_ms, &min_cp_100ms, &ecm_rep_period_ms);
  free(buf);
  return 0;
}
