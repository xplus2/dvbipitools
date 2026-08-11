/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* AFL++ harness: feeds file argv[1] as one datagram into dvbstp_reasm_feed().
   Reachable from any host on the joined multicast group, not just a trusted
   headend. Build with afl-cc/afl-clang-fast for instrumentation. */

#include <stdio.h>
#include <stdlib.h>

#include "lib/net/dvbstp.h"

int main(int argc, char **argv) {
  FILE *f;
  unsigned char *buf;
  long len;
  size_t n;
  dvbstp_reasm_t *r;
  dvbstp_header_t h;
  const unsigned char *out_data;
  size_t out_len;

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

  r = dvbstp_reasm_new();
  if (r) {
    dvbstp_reasm_feed(r, buf, n, &h, &out_data, &out_len);
    dvbstp_reasm_free(r);
  }

  free(buf);
  return 0;
}
