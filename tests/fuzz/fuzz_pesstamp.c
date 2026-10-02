/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* AFL++ harness: feeds file argv[1] as consecutive 188-byte TS packets
   into the PES/adaptation field timestamp helpers. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipitvhead/mux/pesstamp.h"

#define TS_PACKET_LEN 188

int main(int argc, char **argv) {
  FILE *f;
  unsigned char *buf;
  long len;
  size_t n, off;
  int64_t delta = 90000;

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

  for (off = 0; off + TS_PACKET_LEN <= n; off += TS_PACKET_LEN) {
    unsigned char pkt[TS_PACKET_LEN];
    pes_stamp_t st;
    memcpy(pkt, buf + off, sizeof pkt);
    pesstamp_read(pkt, &st);
    pesstamp_shift(pkt, delta);
    afstamp_shift(pkt, -delta);
    afstamp_clear_discontinuity(pkt);
    delta = delta * 3 + (int64_t)pkt[off % TS_PACKET_LEN];
    if (delta > ((int64_t)1 << 40) || delta < -((int64_t)1 << 40))
      delta = 1;
  }

  free(buf);
  return 0;
}
