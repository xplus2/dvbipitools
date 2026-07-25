/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* AFL++ harness: feeds file argv[1] to accessunit_decode() using dipibim's
 * exact on-disk framing (4-byte BE bits_len, bitstream, string repo).
 * Build with afl-cc/afl-clang-fast for instrumentation. */

#include <stdio.h>
#include <stdlib.h>

#include "lib/bim/accessunit.h"
#include "lib/bim/bitreader.h"
#include "lib/tva/epg_doc.h"

int main(int argc, char **argv) {
  FILE *f;
  unsigned char *buf;
  long len;
  size_t n, bits_len;
  bitreader_t br;
  strrepo_reader_t sr;
  epg_doc_t doc;
  int nfuu = 0;

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
  if (len < 4 || fseek(f, 0, SEEK_SET)) {
    fclose(f);
    return 0;
  }
  buf = malloc((size_t)len);
  if (!buf) {
    fclose(f);
    return 1;
  }
  n = fread(buf, 1, (size_t)len, f);
  fclose(f);
  if (n < 4) {
    free(buf);
    return 0;
  }

  bits_len = ((size_t)buf[0] << 24) | ((size_t)buf[1] << 16) | ((size_t)buf[2] << 8) | (size_t)buf[3];
  if (bits_len > n - 4) {
    free(buf);
    return 0;
  }

  epg_doc_init(&doc);
  bitreader_init(&br, buf + 4, bits_len);
  if (!strrepo_reader_init(&sr, buf + 4 + bits_len, n - 4 - bits_len))
    accessunit_decode(&br, &sr, &doc, &nfuu);
  epg_doc_free(&doc);

  free(buf);
  return 0;
}
