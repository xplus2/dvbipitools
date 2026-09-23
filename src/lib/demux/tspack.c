/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "tspack.h"

/* scans forward from d[1] for the next 0x47 sync byte within len bytes.
   returns the number of bytes to skip (>=1) */
static size_t resync_skip(const unsigned char *d, size_t len) {
  size_t k = 1;
  while (k < len && d[k] != 0x47) k++;
  return k;
}

void tspack_sync_good(tspack_sync_t *s) {
  s->bad_run = 0;
  if (s->good_run < 5 && ++s->good_run == 5) s->in_sync = 1;
}

void tspack_sync_bad(tspack_sync_t *s) {
  s->byte_errors++;
  s->good_run = 0;
  if (s->bad_run < 2 && ++s->bad_run == 2 && s->in_sync) {
    s->in_sync = 0;
    s->losses++;
  }
}

static inline void sync_boundary(tspack_sync_t *s, int ok) {
  if (!ok) {
    tspack_sync_bad(s);
    s->candidate = 1;
  } else if (s->candidate) {
    s->candidate = 0;
  } else tspack_sync_good(s);
}

static inline int feed_core(tspack_t *pz, const unsigned char *d, size_t len, int (*cb)(void *, const unsigned char *), void *ctx, tspack_sync_t *sync) {
  while (len) {
    const unsigned char *p = NULL;
    if (pz->acclen) {
      size_t need = 188 - pz->acclen;
      if (need > len) need = len;
      memcpy(pz->acc + pz->acclen, d, need);
      pz->acclen += need;
      d += need;
      len -= need;
      if (pz->acclen < 188) continue;
      pz->acclen = 0;
      p = pz->acc;
      if (sync) sync_boundary(sync, p[0] == 0x47);
    } else if (len >= 188) {
      if (d[0] != 0x47) {
        size_t k = resync_skip(d, len);
        if (sync) sync_boundary(sync, 0);
        d += k;
        len -= k;
        continue;
      }
      if (sync) sync_boundary(sync, 1);
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
      if (r) return r;
    }
  }
  return 0;
}

int tspack_feed(tspack_t *pz, const unsigned char *d, size_t len, int (*cb)(void *, const unsigned char *), void *ctx) {
  return feed_core(pz, d, len, cb, ctx, NULL);
}

int tspack_feed_sync(tspack_t *pz, const unsigned char *d, size_t len, int (*cb)(void *, const unsigned char *), void *ctx, tspack_sync_t *sync) {
  return feed_core(pz, d, len, cb, ctx, sync);
}

unsigned tspack_pid(const unsigned char *pkt) {
  return (((unsigned)pkt[1] & 0x1F) << 8) | pkt[2];
}

size_t tspack_length12(const unsigned char *p) {
  return (((size_t)p[0] & 0x0F) << 8) | p[1];
}

int tspack_payload(const unsigned char *pkt, const unsigned char **pl, size_t *plen, int *pusi) {
  unsigned afc = (pkt[3] >> 4) & 0x3;
  size_t off;
  if (afc == 0 || afc == 2) return 0;
  off = 4;
  if (afc == 3) {
    off = 5 + (size_t)pkt[4];
    if (off >= 188) return 0;
  }
  *pl = pkt + off;
  *plen = 188 - off;
  *pusi = pkt[1] & 0x40;
  return 1;
}
