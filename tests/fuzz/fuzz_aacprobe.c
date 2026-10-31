/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* AFL++ harness: feeds file argv[1] to aacprobe_feed() and asc_parse(). Build with afl-cc/afl-clang-fast for instrumentation. */

#include <stdio.h>
#include <stdlib.h>

#include "lib/demux/aacprobe/aacprobe.h"
#include "lib/demux/escodec/escodec.h"

#define MAX_INPUT 65536

int main(int argc, char **argv) {
  static unsigned char buf[MAX_INPUT];
  FILE *f;
  size_t len;
  aacprobe_t p;
  asc_info_t a;
  if (argc != 2) return 1;
  f = fopen(argv[1], "rb");
  if (!f) return 1;
  len = fread(buf, 1, sizeof buf, f);
  fclose(f);
  if (!len) return 0;
  asc_parse(buf + 1, len - 1, &a);
  aacprobe_init(&p, buf[0] & 0x0F, 48000);
  aacprobe_feed(&p, buf + 1, len - 1);
  aacprobe_feed(&p, buf + 1, len - 1);
  return 0;
}
