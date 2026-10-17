/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/helper/log.h"
#include "lib/mux/mp4/priv.h"

#define CK_MARGIN 3 /* open run, provisional sample, closing run */
#define CK_MIN_ENTRIES 256
#define CK_MIN_SLACK (64 * 1024) /* below: relayouts would be constant */

/* moov + trailing free header into region */
int fits_region(const mp4buf_t *moov) {
  size_t rest;
  if (moov->len > MP4_MOOV_RESERVE) return 0;
  rest = MP4_MOOV_RESERVE - moov->len;
  return rest == 0 || rest >= 8;
}

void write_region(mp4_t *m, const mp4buf_t *moov) {
  unsigned char buf[8];
  size_t rest = MP4_MOOV_RESERVE - moov->len;
  unsigned char *all = malloc(moov->len + (rest ? 8 : 0));

  if (!all) {
    m->err = 1;
    return;
  }
  memcpy(all, moov->p, moov->len);
  if (rest) {
    put_be32(buf, (uint32_t)rest);
    memcpy(buf + 4, "free", 4);
    memcpy(all + moov->len, buf, 8);
  }
  if (pwrite(m->fd, all, moov->len + (rest ? 8 : 0), (off_t)m->moov_pos) != (ssize_t)(moov->len + (rest ? 8 : 0)))
    m->err = 1;
  free(all);
}

static uint32_t get_be32(const unsigned char *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int ck_child(const unsigned char *img, size_t lo, size_t hi, const char *tag, size_t *start, size_t *end) {
  while (lo + 8 <= hi) {
    size_t sz = get_be32(img + lo);
    if (sz < 8 || sz > hi - lo) return 0;
    if (!memcmp(img + lo + 4, tag, 4)) {
      *start = lo;
      *end = lo + sz;
      return 1;
    }
    lo += sz;
  }
  return 0;
}

static int ck_locate(mp4_t *m) {
  const unsigned char *img = m->ck_moov.p;
  size_t hi = m->ck_moov.len;
  size_t lo = 8;
  size_t s;
  size_t e;

  if (!ck_child(img, lo, hi, "mvhd", &s, &e)) return 0;
  m->ck_mvhd = s + 24;
  lo = e;
  for (int i = 0; i < m->ntrk; i++) {
    track_t *t = &m->trk[i];
    const char *tags[CK_NTAB] = {"stts", "ctts", "stss", "stsz", t->ck_wide ? "co64" : "stco"};
    size_t ts;
    size_t te;
    size_t ms;
    size_t me;
    size_t ns;
    size_t ne;
    size_t ss;
    size_t se;

    if (!ck_child(img, lo, hi, "trak", &ts, &te)) return 0;
    lo = te;
    if (!ck_child(img, ts + 8, te, "tkhd", &s, &e)) return 0;
    t->ck_tkhd = s + 28;
    t->ck_elst = 0;
    if (ck_child(img, ts + 8, te, "edts", &s, &e) && ck_child(img, s + 8, e, "elst", &s, &e))
      t->ck_elst = s + 16 + (size_t)(get_be32(img + s + 12) - 1) * 12;
    if (!ck_child(img, ts + 8, te, "mdia", &ms, &me) || !ck_child(img, ms + 8, me, "mdhd", &s, &e)) return 0;
    t->ck_mdhd = s + 24;
    if (!ck_child(img, ms + 8, me, "minf", &ns, &ne) || !ck_child(img, ns + 8, ne, "stbl", &ss, &se)) return 0;
    for (int k = 0; k < CK_NTAB; k++) {
      ck_tab_t *c = &t->ck[k];
      if (!c->esz) continue;
      if (!ck_child(img, ss + 8, se, tags[k], &s, &e)) return 0;
      c->pos = s + 8 + (k == CK_STSZ ? 12 : 8);
    }
  }
  return 1;
}

static void ck_put(const ck_tab_t *c, unsigned char *img, uint32_t idx, uint64_t v) {
  unsigned char *p = img + c->pos + (size_t)idx * c->esz;
  if (c->esz == 8)
    put_be64(p, v);
  else
    put_be32(p, (uint32_t)v);
}

static void ck_put_run(const ck_tab_t *c, unsigned char *img, uint32_t idx, uint32_t cnt, uint32_t val) {
  unsigned char *p = img + c->pos + (size_t)idx * 8;
  put_be32(p, cnt);
  put_be32(p + 4, val);
}

/* closed runs into image, open run stays in table state */
static int ck_fold_run(track_t *t, unsigned char *img, int kind, uint32_t end) {
  ck_tab_t *c = &t->ck[kind];
  for (uint32_t i = c->folded; i < end; i++) {
    uint32_t v = kind == CK_STTS ? t->samp[i].duration : (uint32_t)t->samp[i].cts_offset;
    if (kind == CK_STTS) t->ck_dur += v;
    if (c->run_cnt && v == c->run_val) {
      c->run_cnt++;
      continue;
    }
    if (c->run_cnt) {
      if (c->closed + 3 > c->cap) return -1;
      ck_put_run(c, img, c->closed++, c->run_cnt, c->run_val);
    }
    c->run_val = v;
    c->run_cnt = 1;
  }
  c->folded = end;
  return 0;
}

/* open run and provisional last sample after closed runs */
static void ck_tail_run(track_t *t, unsigned char *img, int kind, int prov, uint32_t pv) {
  ck_tab_t *c = &t->ck[kind];
  uint32_t k = c->closed;
  if (prov && c->run_cnt && pv == c->run_val) {
    ck_put_run(c, img, k++, c->run_cnt + 1, c->run_val);
  } else {
    if (c->run_cnt) ck_put_run(c, img, k++, c->run_cnt, c->run_val);
    if (prov) ck_put_run(c, img, k++, 1, pv);
  }
  c->n = k;
}

static int ck_fold_list(track_t *t, unsigned char *img, int kind) {
  ck_tab_t *c = &t->ck[kind];
  for (uint32_t i = c->folded; i < (uint32_t)t->nsamp; i++) {
    const mp4_samp_t *s = &t->samp[i];
    uint64_t v;
    if (kind == CK_STSS) {
      if (!s->keyframe) continue;
      v = i + 1u;
    } else if (kind == CK_STSZ) {
      v = s->size;
    } else {
      v = s->offset;
      if (!t->ck_wide && v > 0xFFFFFFFFu) return -1;
    }
    if (c->n >= c->cap) return -1;
    ck_put(c, img, c->n++, v);
  }
  c->folded = (uint32_t)t->nsamp;
  return 0;
}

void ck_pwrite(mp4_t *m, const unsigned char *p, size_t n, size_t pos) {
  while (n && !m->err) {
    ssize_t w = pwrite(m->fd, p, n, (off_t)(m->moov_pos + pos));
    if (w < 0) {
      if (errno == EINTR) continue;
      log_code("LE100125", "mp4 write: %s", strerror(errno));
      m->err = 1;
      return;
    }
    p += w;
    pos += (size_t)w;
    n -= (size_t)w;
  }
}

static void ck_flush_tab(mp4_t *m, const ck_tab_t *c) {
  const unsigned char *img = m->ck_moov.p;
  if (c->n > c->synced) ck_pwrite(m, img + c->pos + (size_t)c->synced * c->esz, (size_t)(c->n - c->synced) * c->esz, c->pos + (size_t)c->synced * c->esz);
  ck_pwrite(m, img + c->pos - 4, 4, c->pos - 4);
}

static int ck_update(mp4_t *m, int flush) {
  unsigned char *img = m->ck_moov.p;
  uint32_t movie = 0;

  for (int i = 0; i < m->ntrk; i++) {
    track_t *t = &m->trk[i];
    uint32_t n = (uint32_t)t->nsamp;
    int prov = t->cls == PID_VIDEO && n;
    uint32_t pv = prov ? t->samp[n - 1].duration : 0;
    track_edit_t e = track_edit(t, m->t0);
    uint32_t d;
    uint32_t seg;

    if (ck_fold_run(t, img, CK_STTS, n - (uint32_t)prov) < 0) return -1;
    ck_tail_run(t, img, CK_STTS, prov, pv);
    if (t->ck[CK_CTTS].esz) {
      if (ck_fold_run(t, img, CK_CTTS, n) < 0) return -1;
      ck_tail_run(t, img, CK_CTTS, 0, 0);
    }
    for (int k = CK_STSS; k < CK_NTAB; k++) {
      if (t->ck[k].esz && ck_fold_list(t, img, k) < 0) return -1;
    }
    d = (uint32_t)(t->ck_dur + pv);
    seg = track_movie_duration_ms(d, &e);
    if (seg > movie) movie = seg;
    put_be32(img + t->ck_tkhd, seg);
    put_be32(img + t->ck_mdhd, d);
    if (t->ck_elst) put_be32(img + t->ck_elst, d > e.skip_ms ? d - e.skip_ms : 0);
  }
  put_be32(img + m->ck_mvhd, movie);
  for (int i = 0; i < m->ntrk; i++) {
    track_t *t = &m->trk[i];
    for (int k = 0; k < CK_NTAB; k++) {
      ck_tab_t *c = &t->ck[k];
      if (!c->esz) continue;
      put_be32(img + c->pos - 4, c->n);
      if (flush) ck_flush_tab(m, c);
      c->synced = (k == CK_STTS || k == CK_CTTS) ? c->closed : c->n;
    }
    if (flush) {
      ck_pwrite(m, img + t->ck_tkhd, 4, t->ck_tkhd);
      ck_pwrite(m, img + t->ck_mdhd, 4, t->ck_mdhd);
      if (t->ck_elst) ck_pwrite(m, img + t->ck_elst, 4, t->ck_elst);
    }
  }
  if (flush) ck_pwrite(m, img + m->ck_mvhd, 4, m->ck_mvhd);
  return 0;
}

static uint32_t count_runs(const track_t *t, int ctts) {
  uint32_t runs = 0;
  uint32_t val = 0;
  for (int i = 0; i < t->nsamp; i++) {
    uint32_t v = ctts ? (uint32_t)t->samp[i].cts_offset : t->samp[i].duration;
    if (!i || v != val) runs++;
    val = v;
  }
  return runs;
}

static uint32_t count_keys(const track_t *t) {
  uint32_t n = 0;
  for (int i = 0; i < t->nsamp; i++) n += t->samp[i].keyframe != 0;
  return n;
}

static void ck_plan_track(track_t *t, uint32_t used[CK_NTAB]) {
  uint32_t n = (uint32_t)t->nsamp;
  memset(t->ck, 0, sizeof t->ck);
  memset(used, 0, CK_NTAB * sizeof *used);
  t->ck_wide = n && t->samp[n - 1].offset > 0xFFFFFFFFu;
  t->ck_had = n > 0;
  t->ck_dur = 0;
  t->ck[CK_STTS].esz = 8;
  used[CK_STTS] = count_runs(t, 0);
  if (t->cls == PID_VIDEO) {
    t->ck[CK_CTTS].esz = 8;
    used[CK_CTTS] = count_runs(t, 1);
  }
  if (t->cls == PID_VIDEO || t->es.codec == CODEC_AC4) {
    t->ck[CK_STSS].esz = 4;
    used[CK_STSS] = count_keys(t);
  }
  t->ck[CK_STSZ].esz = 4;
  used[CK_STSZ] = n;
  t->ck[CK_CO].esz = t->ck_wide ? 8 : 4;
  used[CK_CO] = n;
}

/* new image with growth room in every table, filled from all samples */
static void ck_layout(mp4_t *m) {
  uint32_t used[MP4_MAX_TRACKS][CK_NTAB];
  mp4buf_t top;
  double weight = 0;
  double fixed = 0;
  double room;
  double f;
  size_t base;
  size_t rest;
  unsigned char hdr[8];

  for (int i = 0; i < m->ntrk; i++) ck_plan_track(&m->trk[i], used[i]);
  if (!build_moov(m, &top, 1)) {
    m->err = 1;
    return;
  }
  base = top.len;
  mp4buf_free(&top);
  for (int i = 0; i < m->ntrk; i++) {
    for (int k = 0; k < CK_NTAB; k++) {
      const ck_tab_t *c = &m->trk[i].ck[k];
      fixed += (double)c->esz * (used[i][k] + CK_MARGIN);
      weight += (double)c->esz * (used[i][k] + CK_MIN_ENTRIES);
    }
  }
  room = (double)(MP4_MOOV_RESERVE - 8) - (double)base - fixed;
  if (room < CK_MIN_SLACK) {
    log_code("LW700127", "mp4: index outgrew reserved region, no further crash checkpoints");
    m->ckpt_full = 1;
    return;
  }
  f = room / weight < 1.0 ? room / weight : 1.0;
  for (int i = 0; i < m->ntrk; i++) {
    for (int k = 0; k < CK_NTAB; k++) {
      ck_tab_t *c = &m->trk[i].ck[k];
      c->cap = used[i][k] + CK_MARGIN + (uint32_t)((used[i][k] + CK_MIN_ENTRIES) * f);
    }
  }
  if (!build_moov(m, &top, 1)) {
    m->err = 1;
    return;
  }
  mp4buf_free(&m->ck_moov);
  m->ck_moov = top;
  if (!ck_locate(m) || ck_update(m, 0) < 0) {
    m->err = 1;
    return;
  }
  rest = MP4_MOOV_RESERVE - top.len;
  put_be32(hdr, (uint32_t)rest);
  memcpy(hdr + 4, "free", 4);
  ck_pwrite(m, hdr, sizeof hdr, top.len);
  ck_pwrite(m, top.p, top.len, 0);
  m->ckpt_done = 1;
}

static int ck_stale(const mp4_t *m) {
  for (int i = 0; i < m->ntrk; i++) {
    if (m->trk[i].nsamp && !m->trk[i].ck_had) return 1;
  }
  return 0;
}

void p4_checkpoint(mp4_t *m) {
  if (!m->started || m->err || m->ckpt_full) return;
  for (int i = 0; i < m->ntrk; i++) {
    track_t *t = &m->trk[i];
    if (t->cls == PID_VIDEO && t->prev_dur_slot && !*t->prev_dur_slot) *t->prev_dur_slot = t->last_dur ? t->last_dur : 1; /* next frame overwrites */
  }
  if (m->ck_moov.p && !ck_stale(m) && ck_update(m, 1) == 0) return;
  ck_layout(m);
}
