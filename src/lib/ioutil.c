/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include "ioutil.h"

int read_all(FILE *f, char **out, size_t *out_len) {
  size_t cap = 65536, len = 0;
  char *buf = malloc(cap);
  if (!buf)
    return -1;
  for (;;) {
    size_t n;
    if (len + 4096 + 1 > cap) {
      char *p;
      cap *= 2;
      p = realloc(buf, cap);
      if (!p) {
        free(buf);
        return -1;
      }
      buf = p;
    }
    n = fread(buf + len, 1, 4096, f);
    len += n;
    if (n < 4096)
      break;
  }
  buf[len] = '\0';
  *out = buf;
  *out_len = len;
  return 0;
}
