/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* AFL++ harness: first byte of file argv[1] picks the target, the rest is the input.
   even: container_parse(). odd: wrapper_parse() */

#include <stdio.h>
#include <stdlib.h>

#include "dipibcg/container.h"
#include "dipibcg/wrapper.h"

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
  unsigned char *buf;
  size_t len;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <input-file>\n", argv[0]);
    return 1;
  }
  buf = read_input(argv[1], &len);
  if (!buf) return 1;
  if (len > 0 && (buf[0] & 1)) {
    unsigned char *out = NULL;
    size_t out_len = 0;

    if (wrapper_parse(buf + 1, len - 1, &out, &out_len) == 0) free(out);
  } else if (len > 0) {
    const unsigned char *au;
    const unsigned char *sr;
    size_t au_len;
    size_t sr_len;

    container_parse(buf + 1, len - 1, &au, &au_len, &sr, &sr_len);
  }
  free(buf);
  return 0;
}
