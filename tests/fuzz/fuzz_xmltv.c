/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* AFL++ harness: first byte of file argv[1] picks the target, rest is input.
   even: xmltv_read(). odd: mapping_load() on a temporary file */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "lib/tva/bcg_doc.h"
#include "lib/tva/mapping.h"
#include "lib/tva/xmltv.h"

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

static void fuzz_mapping(const unsigned char *data, size_t len) {
  char path[] = "/tmp/fuzz_mapping_XXXXXX";
  mapping_t m;
  int fd = mkstemp(path);
  if (fd < 0) return;
  if (write(fd, data, len) == (ssize_t)len && mapping_load(path, &m) == 0) mapping_free(&m);
  close(fd);
  unlink(path);
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
    fuzz_mapping(buf + 1, len - 1);
  } else if (len > 0) {
    FILE *f = fmemopen(buf + 1, len - 1 ? len - 1 : 1, "rb");
    if (f) {
      bcg_doc_t doc;
      bcg_doc_init(&doc);
      xmltv_read(f, &doc);
      bcg_doc_free(&doc);
      fclose(f);
    }
  }
  free(buf);
  return 0;
}
