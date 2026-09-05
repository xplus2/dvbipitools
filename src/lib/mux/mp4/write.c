/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "priv.h"

void p4_wfd(mp4_t *m, const void *p, size_t n) {
  const unsigned char *b = p;

  while (n && !m->err) {
    ssize_t w = write(m->fd, b, n);
    if (w < 0) {
      if (errno == EINTR) continue;
      log_line("mp4 write: %s", strerror(errno));
      m->err = 1;
      return;
    }
    b += w;
    n -= (size_t)w;
    *m->bytes += (unsigned long long)w;
  }
}

track_t *p4_find_track(mp4_t *m, unsigned pid) {
  for (int i = 0; i < m->ntrk; i++) if (m->trk[i].pid == pid) return &m->trk[i];
  return NULL;
}

static int samp_reserve(track_t *t, int need) {
  size_t cap = (size_t)t->samp_cap;
  int rc = growbuf_reserve((void **)&t->samp, &cap, sizeof *t->samp, (size_t)need, 64);
  t->samp_cap = (int)cap;
  return rc;
}

void p4_write_sample(mp4_t *m, track_t *t, int32_t cts, const unsigned char *d, size_t n, int key, uint32_t dur) {
  mp4_samp_t *s;
  uint64_t off;
  if (samp_reserve(t, t->nsamp + 1) < 0) {
    m->err = 1;
    return;
  }
  off = *m->bytes;
  p4_wfd(m, d, n);
  if (m->err) return;
  s = &t->samp[t->nsamp++];
  s->offset = off;
  s->size = (uint32_t)n;
  s->duration = dur;
  s->cts_offset = cts;
  s->keyframe = key;
  if (t->cls == PID_VIDEO)
    t->prev_dur_slot = &s->duration;
}

void p4_pend_add(mp4_t *m, int trk, int64_t ts, int32_t cts, const unsigned char *d, size_t n, int key, uint32_t dur) {
  pend_t *p;
  track_t *t = &m->trk[trk];
  if (m->npend >= MP4_PEND_MAX || m->pend_bytes + n > MP4_PEND_BYTES) {
    p4_start(m); /* give up on silent track */
    if (m->started) p4_write_sample(m, t, cts, d, n, key, dur);
    return;
  }
  p = &m->pend[m->npend];
  p->data = m->pend_arena + m->pend_bytes;
  memcpy(p->data, d, n);
  p->trk = trk;
  p->ts_ms = ts;
  p->cts_offset = cts;
  p->len = n;
  p->key = key;
  p->dur = dur;
  m->npend++;
  m->pend_bytes += n;
  if (t->cls == PID_VIDEO)
    t->prev_dur_slot = &p->dur;
}

void p4_route(mp4_t *m, track_t *t, int64_t ts_ms, int32_t cts, const unsigned char *d, size_t n, int key, uint32_t dur) {
  if (!m->started) {
    p4_pend_add(m, (int)(t - m->trk), ts_ms, cts, d, n, key, dur);
    return;
  }
  if (ts_ms < m->t0) return;
  p4_write_sample(m, t, cts, d, n, key, dur);
}

void p4_start(mp4_t *m) {
  if (m->started || !m->ntrk) return;
  m->t0 = 0;
  if (m->npend) {
    int vtrk = -1, found = 0;
    int64_t vt = 0;
    for (int i = 0; i < m->ntrk; i++) if (m->trk[i].cls == PID_VIDEO) {
      vtrk = i;
      break;
    }
    m->t0 = m->pend[0].ts_ms;
    for (int i = 1; i < m->npend; i++) if (m->pend[i].ts_ms < m->t0) m->t0 = m->pend[i].ts_ms;
    for (int i = 0; vtrk >= 0 && i < m->npend; i++) if (m->pend[i].trk == vtrk && (!found || m->pend[i].ts_ms < vt)) {
      vt = m->pend[i].ts_ms;
      found = 1;
    }
    if (found) m->t0 = vt;
  }
  p4_write_ftyp_mdat_head(m);
  m->started = 1;
  for (int i = 0; i < m->npend; i++) if (m->pend[i].ts_ms >= m->t0) {
    track_t *t = &m->trk[m->pend[i].trk];
    p4_write_sample(m, t, m->pend[i].cts_offset, m->pend[i].data, m->pend[i].len, m->pend[i].key, m->pend[i].dur);
  }
  m->npend = 0;
  m->pend_bytes = 0;
}
