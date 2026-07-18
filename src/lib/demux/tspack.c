/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "tspack.h"

int tspack_feed(tspack_t *pz, const unsigned char *d, size_t len, int (*cb)(void *, const unsigned char *), void *ctx) {
  while (len) {
    const unsigned char *p = NULL;
    if (pz->acclen) {
      size_t need = 188 - pz->acclen;
      if (need > len)
        need = len;
      memcpy(pz->acc + pz->acclen, d, need);
      pz->acclen += need;
      d += need;
      len -= need;
      if (pz->acclen < 188)
        continue;
      pz->acclen = 0;
      p = pz->acc;
    } else if (len >= 188) {
      if (d[0] != 0x47) {
        size_t k = 1;
        while (k < len && d[k] != 0x47)
          k++;
        d += k;
        len -= k;
        continue;
      }
      p = d;
      d += 188;
      len -= 188;
    } else {
      memcpy(pz->acc, d, len);
      pz->acclen = len;
      len = 0;
    }
    if (p) {
      int r = cb(ctx, p);
      if (r)
        return r;
    }
  }
  return 0;
}
