/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* AFL++ harness: feeds file argv[1] to store_ingest() as one datagram, then as
   a 2nd one so restart and part-commit paths see repeated input */

#include <stdio.h>
#include <stdlib.h>

#include "lib/metrics/store.h"

static unsigned char *read_input(const char *path, size_t *len_out) {
  FILE *f = fopen(path, "rb");
  unsigned char *buf;
  long len;
  size_t n;

  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
    fclose(f);
    return NULL;
  }
  buf = malloc((size_t)len + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  n = fread(buf, 1, (size_t)len, f);
  fclose(f);
  buf[n] = '\0';
  *len_out = n;
  return buf;
}

int main(int argc, char **argv) {
  static store_t st;
  unsigned char *buf;
  size_t len;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <input-file>\n", argv[0]);
    return 1;
  }
  buf = read_input(argv[1], &len);
  if (!buf) return 1;
  store_init(&st);
  store_ingest(&st, buf, len, 1.0, 1);
  store_ingest(&st, buf, len, 2.0, 1);
  store_reap_expired(&st, 100.0, 5.0);
  store_free(&st);
  free(buf);
  return 0;
}
