/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* AFL++ harness: feeds file argv[1] to rtcp_parse(). Build with afl-cc/afl-clang-fast for instrumentation. */

#include <stdio.h>
#include <stdlib.h>

#include "lib/demux/rtcp.h"

static void on_nack(const rtcp_nack_t *nack, void *user) {
  (void)nack;
  (void)user;
}

static void on_rams_r(const rtcp_rams_r_t *req, void *user) {
  (void)req;
  (void)user;
}

static void on_rams_t(const rtcp_rams_t_t *term, void *user) {
  (void)term;
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

  rtcp_parse(buf, n, on_nack, on_rams_r, on_rams_t, NULL);

  free(buf);
  return 0;
}
