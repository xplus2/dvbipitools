/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* AFL++ harness: NUL-terminates file argv[1] and feeds it to
   ssdp_msearch_header() as the header block of an M-SEARCH request.
   Reachable from any host on the SSDP multicast group. Build with
   afl-cc/afl-clang-fast for instrumentation. */

#include <stdio.h>
#include <stdlib.h>

#include "dipixy/dlna/ssdp.h"

int main(int argc, char **argv) {
  FILE *f;
  char *buf;
  long len;
  size_t n;
  char out[192];

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
  buf = malloc((size_t)len + 1);
  if (!buf) {
    fclose(f);
    return 1;
  }
  n = fread(buf, 1, (size_t)len, f);
  fclose(f);
  buf[n] = '\0';

  ssdp_msearch_header(buf, "ST", out, sizeof out);

  free(buf);
  return 0;
}
