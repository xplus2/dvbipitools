/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>
#include "pes.h"

#define PES_MAX 16

typedef struct {
  unsigned pid;
  int used, started, has_pts;
  uint64_t pts;
  unsigned char *buf;
  size_t len, cap;
} stream_t;

struct pes {
  pes_cb cb;
  void *ctx;
  stream_t s[PES_MAX];
  int n;
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
  int i;
  if (!p)
    return;
  for (i = 0; i < p->n; i++)
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
  int i;
  for (i = 0; i < p->n; i++)
    if (p->s[i].pid == pid)
      return &p->s[i];
  return NULL;
}

static int sgrow(stream_t *s, size_t need) {
  if (s->len + need > s->cap) {
    size_t nc = s->cap ? s->cap * 2 : 8192;
    unsigned char *np;
    while (nc < s->len + need)
      nc *= 2;
    np = realloc(s->buf, nc);
    if (!np)
      return -1;
    s->buf = np;
    s->cap = nc;
  }
  return 0;
}

static void deliver(pes_t *p, stream_t *s) {
  if (s->started && s->len)
    p->cb(p->ctx, s->pid, s->has_pts, s->pts, s->buf, s->len);
  s->started = 0;
  s->len = 0;
}

static void append(stream_t *s, const unsigned char *d, size_t n) {
  if (sgrow(s, n) == 0) {
    memcpy(s->buf + s->len, d, n);
    s->len += n;
  }
}

void pes_feed(pes_t *p, const unsigned char *pkt) {
  unsigned pid, afc;
  int pusi;
  size_t off, plen;
  const unsigned char *pl;
  stream_t *s;

  if (pkt[0] != 0x47)
    return;
  pid = (((unsigned)pkt[1] & 0x1F) << 8) | pkt[2];
  s = find(p, pid);
  if (!s)
    return;
  pusi = pkt[1] & 0x40;
  afc = (pkt[3] >> 4) & 0x3;
  if (afc == 0 || afc == 2)
    return;
  off = 4;
  if (afc == 3) {
    off = 5 + (size_t)pkt[4];
    if (off >= 188)
      return;
  }
  pl = pkt + off;
  plen = 188 - off;

  if (pusi) {
    unsigned hdrlen;
    size_t es;
    deliver(p, s);
    if (plen < 9 || pl[0] || pl[1] || pl[2] != 1) /* need 00 00 01 */
      return;
    hdrlen = pl[8];
    es = 9 + (size_t)hdrlen;
    s->has_pts = (pl[7] & 0x80) ? 1 : 0;
    if (s->has_pts)
      s->pts = read_pts(pl + 9);
    s->started = 1;
    if (es < plen)
      append(s, pl + es, plen - es);
  } else if (s->started) {
    append(s, pl, plen);
  }
}

void pes_flush(pes_t *p) {
  int i;
  for (i = 0; i < p->n; i++)
    deliver(p, &p->s[i]);
}
