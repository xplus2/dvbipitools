/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* AFL++ harness: feeds file argv[1] to the WebSocket frame decoder as one
   client read, then drains all reassembled messages. Build with
   afl-cc/afl-clang-fast for instrumentation. */

#include <stdio.h>
#include <stdlib.h>

#include "dipixy/ws/ws_frame.h"

int main(int argc, char **argv) {
  FILE *f;
  uint8_t *buf;
  long len;
  size_t n;
  ws_parser_t p;
  int opcode, rc;
  const uint8_t *payload;
  size_t payload_len;

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

  ws_parser_init(&p);
  if (ws_parser_feed(&p, buf, n) == 0) {
    do {
      rc = ws_parser_next(&p, &opcode, &payload, &payload_len);
    } while (rc == 1);
  }
  ws_parser_free(&p);

  free(buf);
  return 0;
}
