/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "pes.h"
#include "tspack.h"

#define PES_MAX 16

typedef struct {
  unsigned pid;
  int used, started, has_pts, has_dts;
  uint64_t pts, dts;
  unsigned char *buf;
  size_t len, cap;
  log_throttle_t drop_throttle;
} stream_t;

struct pes {
  pes_cb cb;
  void *ctx;
  stream_t s[PES_MAX];
  int n;
  int last_idx; /* MRU 1-entry cache: consecutive packets are usually the same pid */
};

static uint64_t read_pts(const unsigned char *p) {
  return ((uint64_t)(p[0] & 0x0E) << 29) | ((uint64_t)p[1] << 22) | ((uint64_t)(p[2] & 0xFE) << 14) | ((uint64_t)p[3] << 7) | ((uint64_t)p[4] >> 1);
}

pes_t *pes_new(pes_cb cb, void *ctx) {
  pes_t *p = calloc(1, sizeof *p);
  if (!p)
    return NULL;
  p->cb = cb;
  p->ctx = ctx;
  return p;
}

void pes_free(pes_t *p) {
  if (!p)
    return;
  for (int i = 0; i < p->n; i++)
    free(p->s[i].buf);
  free(p);
}

int pes_track(pes_t *p, unsigned pid) {
  if (p->n >= PES_MAX)
    return -1;
  p->s[p->n].pid = pid;
  p->s[p->n].used = 1;
  p->n++;
  return 0;
}

static stream_t *find(pes_t *p, unsigned pid) {
  if (p->last_idx >= 0 && p->last_idx < p->n && p->s[p->last_idx].pid == pid)
    return &p->s[p->last_idx];

  for (int i = 0; i < p->n; i++)
    if (p->s[i].pid == pid) {
      p->last_idx = i;
      return &p->s[i];
    }
  return NULL;
}

static int sgrow(stream_t *s, size_t need) {
  return growbuf_reserve((void **)&s->buf, &s->cap, 1, s->len + need, 8192);
}

static void deliver(pes_t *p, stream_t *s) {
  if (s->started && s->len)
    p->cb(p->ctx, s->pid, s->has_pts, s->pts, s->has_dts, s->dts, s->buf, s->len);
  s->started = 0;
  s->len = 0;
}

static void append(stream_t *s, const unsigned char *d, size_t n) {
  if (sgrow(s, n) == 0) {
    memcpy(s->buf + s->len, d, n);
    s->len += n;
  } else {
    log_throttled(&s->drop_throttle, LOG_THROTTLE_WINDOW_S, "pes: sgrow failed, pid %u payload bytes dropped", s->pid);
  }
}

void pes_feed(pes_t *p, const unsigned char *pkt) {
  unsigned pid;
  int pusi;
  size_t plen;
  const unsigned char *pl;
  stream_t *s;

  if (pkt[0] != 0x47)
    return;
  pid = tspack_pid(pkt);
  s = find(p, pid);
  if (!s)
    return;
  if (!tspack_payload(pkt, &pl, &plen, &pusi))
    return;

  if (pusi) {
    unsigned hdrlen;
    size_t es;
    deliver(p, s);
    if (plen < 9 || pl[0] || pl[1] || pl[2] != 1) /* need 00 00 01 */
      return;
    hdrlen = pl[8];
    es = 9 + (size_t)hdrlen;
    s->has_pts = (pl[7] & 0x80) ? 1 : 0;
    s->has_dts = (pl[7] & 0xC0) == 0xC0 && plen >= 19 ? 1 : 0;
    if (s->has_pts)
      s->pts = read_pts(pl + 9);
    if (s->has_dts)
      s->dts = read_pts(pl + 14);
    s->started = 1;
    if (es < plen)
      append(s, pl + es, plen - es);
  } else if (s->started) {
    append(s, pl, plen);
  }
}

void pes_flush(pes_t *p) {
  for (int i = 0; i < p->n; i++)
    deliver(p, &p->s[i]);
}

#define PTS_MOD 0x200000000ULL  /* 2^33: PTS clock period */
#define PTS_HALF 0x100000000ULL /* 2^32: wrap/backstep threshold */

int64_t pts_unwrap(pts_unwrap_t *st, uint64_t raw) {
  uint64_t d;
  int64_t diff;

  raw &= PTS_MOD - 1;
  if (!st->pts_seen) {
    st->pts_ext = raw;
    st->last_raw = raw;
    st->pts_seen = 1;
  } else {
    d = (raw - st->last_raw) & (PTS_MOD - 1);
    diff = (d >= PTS_HALF) ? (int64_t)d - (int64_t)PTS_MOD : (int64_t)d;
    st->pts_ext = (uint64_t)((int64_t)st->pts_ext + diff);
    st->last_raw = raw;
  }
  return (int64_t)(st->pts_ext / 90);
}
