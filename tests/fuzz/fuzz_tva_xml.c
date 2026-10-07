/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* AFL++ harness: feeds file argv[1] to tva_xml_read() */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>

#include "lib/tva/bcg_doc.h"
#include "lib/tva/tva_xml.h"

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
  FILE *f;
  bcg_doc_t doc;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <input-file>\n", argv[0]);
    return 1;
  }
  buf = read_input(argv[1], &len);
  if (!buf) return 1;
  f = fmemopen(buf, len ? len : 1, "rb");
  if (f) {
    bcg_doc_init(&doc);
    tva_xml_read(f, &doc);
    bcg_doc_free(&doc);
    fclose(f);
  }
  free(buf);
  return 0;
}
