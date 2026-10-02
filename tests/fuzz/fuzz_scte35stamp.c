/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* AFL++ harness: feeds file argv[1] as consecutive 188-byte TS packets
   into the SCTE-35 pts_adjustment patcher. Every packet fed must come out exactly once. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipitvhead/mux/scte35stamp.h"

#define TS_PACKET_LEN 188

static unsigned long emitted;

static void count_emit(void *ctx, unsigned char *pkt188) {
  (void)ctx;
  (void)pkt188;
  emitted++;
}

int main(int argc, char **argv) {
  FILE *f;
  unsigned char *buf;
  long len;
  size_t n, off;
  unsigned long fed = 0;
  scte35stamp_t *st;

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
  st = malloc(sizeof *st);
  if (!buf || !st) {
    fclose(f);
    free(buf);
    free(st);
    return 1;
  }
  n = fread(buf, 1, (size_t)len, f);
  fclose(f);

  scte35stamp_init(st);
  for (off = 0; off + TS_PACKET_LEN <= n; off += TS_PACKET_LEN) {
    unsigned char pkt[TS_PACKET_LEN];
    memcpy(pkt, buf + off, sizeof pkt);
    scte35stamp_feed(st, pkt, (int64_t)(off + 1) * 977, count_emit, NULL);
    fed++;
  }
  scte35stamp_flush(st, count_emit, NULL);
  if (fed != emitted)
    abort();

  free(st);
  free(buf);
  return 0;
}
