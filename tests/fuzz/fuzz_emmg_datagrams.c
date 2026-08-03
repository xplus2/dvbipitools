/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* AFL++ harness: feeds file argv[1] as the payload of an EMMG data_provision message
   (TS 103 197 clause 6.4.7) into emmg_extract_datagrams(). Reachable from any TCP client
   that connects to our EMMG listener, not just a trusted muxer. Build with afl-cc/afl-clang-fast for instrumentation. */

#include <stdio.h>
#include <stdlib.h>

#include "dipitvhead/cas/emmg_server.h"

static void noop_cb(const unsigned char *data, unsigned short len, void *user) {
  (void)data;
  (void)len;
  (void)user;
}

int main(int argc, char **argv) {
  FILE *f;
  unsigned char *buf;
  long len;
  size_t n;

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

  emmg_extract_datagrams(buf, n, noop_cb, NULL);

  free(buf);
  return 0;
}
