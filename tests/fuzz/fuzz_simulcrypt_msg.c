/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* AFL++ harness: feeds file argv[1] as one generic_message (TS 103 197 clause 4.4.1)
   through simulcrypt_hdr_parse() + simulcrypt_tlv_reader_next(). Both ecmg_client.c and
   emmg_server.c depend on this parser for bytes straight off a TCP peer. Build with
   afl-cc/afl-clang-fast for instrumentation. */

#include <stdio.h>
#include <stdlib.h>

#include "lib/cas/simulcrypt_msg.h"

int main(int argc, char **argv) {
  FILE *f;
  unsigned char *buf;
  long len;
  size_t n;
  simulcrypt_hdr_t hdr;
  simulcrypt_tlv_reader_t r;
  unsigned short tag, vlen;
  const unsigned char *val;

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

  if (simulcrypt_hdr_parse(buf, n, &hdr) == 0) {
    size_t avail = n - SIMULCRYPT_HDR_LEN;
    size_t plen = hdr.payload_len < avail ? hdr.payload_len : avail;
    simulcrypt_tlv_reader_init(&r, buf + SIMULCRYPT_HDR_LEN, plen);
    while (simulcrypt_tlv_reader_next(&r, &tag, &val, &vlen) == 1)
      ;
  }

  free(buf);
  return 0;
}
