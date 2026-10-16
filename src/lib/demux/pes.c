/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "pes.h"
#include "tspack.h"

#define PES_MAX 16
#define PES_MAX_BYTES (16u << 20) /* per-PES buffer cap */

typedef struct {
  unsigned pid;
  int used;
  int started;
  int hdr; /* header bytes still buffered in buf */
  int has_pts;
  int has_dts;
  uint64_t pts;
  uint64_t dts;
  unsigned char *buf;
  size_t len;
  size_t cap;
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
  if (s->started && !s->hdr && s->len)
    p->cb(p->ctx, s->pid, s->has_pts, s->pts, s->has_dts, s->dts, s->buf, s->len);
  s->started = 0;
  s->hdr = 0;
  s->len = 0;
}

static void append(stream_t *s, const unsigned char *d, size_t n) {
  if (s->len + n > PES_MAX_BYTES) {
    log_throttled(&s->drop_throttle, LOG_THROTTLE_WINDOW_S, "LW500249", "pes: pid %u PES exceeds %u bytes, dropped until next start", s->pid, PES_MAX_BYTES);
    s->started = 0;
    s->hdr = 0;
    s->len = 0;
    return;
  }
  if (sgrow(s, n) == 0) {
    memcpy(s->buf + s->len, d, n);
    s->len += n;
  } else {
    log_throttled(&s->drop_throttle, LOG_THROTTLE_WINDOW_S, "LW500137", "pes: sgrow failed, pid %u payload bytes dropped", s->pid);
  }
}

static void parse_hdr(stream_t *s) {
  const unsigned char *b = s->buf;
  size_t need;

  if (s->len >= 3 && (b[0] || b[1] || b[2] != 1)) {
    s->started = 0;
    s->hdr = 0;
    s->len = 0;
    return;
  }
  if (s->len < 9)
    return;
  need = 9 + (size_t)b[8];
  if (s->len < need)
    return;
  s->has_pts = (b[7] & 0x80) && b[8] >= 5;
  s->has_dts = (b[7] & 0xC0) == 0xC0 && b[8] >= 10;
  if (s->has_pts)
    s->pts = read_pts(b + 9);
  if (s->has_dts)
    s->dts = read_pts(b + 14);
  s->len -= need;
  memmove(s->buf, s->buf + need, s->len);
  s->hdr = 0;
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
    deliver(p, s);
    s->has_pts = 0;
    s->has_dts = 0;
    s->started = 1;
    s->hdr = 1;
    append(s, pl, plen);
    if (s->len)
      parse_hdr(s);
    else
      s->started = 0;
  } else if (s->started) {
    append(s, pl, plen);
    if (s->hdr)
      parse_hdr(s);
  }
}

void pes_flush(pes_t *p) {
  for (int i = 0; i < p->n; i++)
    deliver(p, &p->s[i]);
}

#define PTS_MOD 0x200000000ULL  /* 2^33: PTS clock period */
#define PTS_HALF 0x100000000ULL /* 2^32: wrap/backstep threshold */

int64_t pts_unwrap_ticks(pts_unwrap_t *st, uint64_t raw) {
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
  return (int64_t)st->pts_ext;
}

int64_t pts_unwrap(pts_unwrap_t *st, uint64_t raw) {
  return pts_unwrap_ticks(st, raw) / 90;
}

static int disc_near(int64_t raw, int64_t shift, int64_t ref) {
  int64_t dev = raw - shift - ref;
  return dev >= -PTS_DISC_MS && dev <= PTS_DISC_MS;
}

int64_t pts_disc_shift(pts_disc_t *d, int64_t raw, int64_t next) {
  if (next == PTS_DISC_NONE || disc_near(raw, d->shift, next)) return d->shift;
  if (d->have_prev && disc_near(raw, d->prev, next)) return d->prev;
  d->prev = d->shift;
  d->have_prev = 1;
  d->shift = raw - next;
  return d->shift;
}

int64_t pts_disc_peek(const pts_disc_t *d, int64_t raw, int64_t ref) {
  if (disc_near(raw, d->shift, ref)) return d->shift;
  if (d->have_prev && disc_near(raw, d->prev, ref)) return d->prev;
  return d->shift;
}
