/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/helper/log.h"
#include "priv.h"

#define CK_MARGIN 3 /* open run, provisional sample, closing run */
#define CK_MIN_ENTRIES 256
#define CK_MIN_SLACK (64 * 1024) /* below: relayouts would be constant */

static void put_be32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}

static void put_be64(unsigned char *p, uint64_t v) {
  put_be32(p, (uint32_t)(v >> 32));
  put_be32(p + 4, (uint32_t)v);
}

void p4_write_ftyp_mdat_head(mp4_t *m) {
  mp4buf_t ftyp;
  mp4buf_t top;
  unsigned char mdat_hdr[16];

  memset(&ftyp, 0, sizeof ftyp);
  if (m->video_ok) {
    mb_fourcc(&ftyp, "isom");
    mb_u32(&ftyp, 0);
    mb_fourcc(&ftyp, "isom");
    mb_fourcc(&ftyp, "iso2");
    mb_fourcc(&ftyp, "avc1");
    mb_fourcc(&ftyp, "mp41");
  } else {
    mb_fourcc(&ftyp, "M4A ");
    mb_u32(&ftyp, 0);
    mb_fourcc(&ftyp, "M4A ");
    mb_fourcc(&ftyp, "mp42");
    mb_fourcc(&ftyp, "isom");
  }
  memset(&top, 0, sizeof top);
  mb_box(&top, "ftyp", &ftyp);
  p4_wfd(m, top.p, top.len);
  mp4buf_free(&top);
  m->moov_pos = *m->bytes;
  put_be32(mdat_hdr, MP4_MOOV_RESERVE);
  memcpy(mdat_hdr + 4, "free", 4);
  p4_wfd(m, mdat_hdr, 8);
  for (size_t left = MP4_MOOV_RESERVE - 8; left && !m->err;) {
    static const unsigned char zero[4096];
    size_t n = left < sizeof zero ? left : sizeof zero;
    p4_wfd(m, zero, n);
    left -= n;
  }
  m->mdat_hdr_pos = *m->bytes;
  put_be32(mdat_hdr, 8);
  memcpy(mdat_hdr + 4, "free", 4);
  put_be32(mdat_hdr + 8, 0); /* size 0: to EOF, stays valid while recording */
  memcpy(mdat_hdr + 12, "mdat", 4);
  p4_wfd(m, mdat_hdr, sizeof mdat_hdr);
}

static void build_mvhd(mp4buf_t *out, int ntrk, uint32_t duration_ms) {
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_u32(&b, 0);
  mb_u32(&b, 0);
  mb_u32(&b, MP4_TIMESCALE);
  mb_u32(&b, duration_ms);
  mb_u32(&b, 0x00010000);
  mb_u16(&b, 0x0100);
  mb_u16(&b, 0);
  mb_u32(&b, 0);
  mb_u32(&b, 0);
  put_matrix_unity(&b);
  for (int i = 0; i < 6; i++) mb_u32(&b, 0);
  mb_u32(&b, (uint32_t)(ntrk + 1));
  mb_box(out, "mvhd", &b);
}

static void build_tkhd(mp4buf_t *out, const track_t *t, uint32_t duration_ms) {
  mp4buf_t b;
  int is_video = (t->cls == PID_VIDEO);
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0x000007);
  mb_u32(&b, 0);
  mb_u32(&b, 0);
  mb_u32(&b, t->track_id);
  mb_u32(&b, 0);
  mb_u32(&b, duration_ms);
  mb_u32(&b, 0);
  mb_u32(&b, 0);
  mb_u16(&b, 0);
  mb_u16(&b, 0);
  mb_u16(&b, (unsigned)(is_video ? 0 : 0x0100));
  mb_u16(&b, 0);
  put_matrix_unity(&b);
  mb_u32(&b, (uint32_t)t->width << 16);
  mb_u32(&b, (uint32_t)t->height << 16);
  mb_box(out, "tkhd", &b);
}

static unsigned mdhd_lang(const char *lang) {
  const char *l = (lang[0] && lang[1] && lang[2]) ? lang : "und";
  return (((unsigned)l[0] - 0x60) & 0x1F) << 10 | (((unsigned)l[1] - 0x60) & 0x1F) << 5 | (((unsigned)l[2] - 0x60) & 0x1F);
}

static void build_mdhd(mp4buf_t *out, const track_t *t, uint32_t duration_ms) {
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_u32(&b, 0);
  mb_u32(&b, 0);
  mb_u32(&b, MP4_TIMESCALE);
  mb_u32(&b, duration_ms);
  mb_u16(&b, mdhd_lang(t->lang));
  mb_u16(&b, 0);
  mb_box(out, "mdhd", &b);
}

static void build_nmhd(mp4buf_t *out) {
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_box(out, "nmhd", &b);
}

static void trak_meta_from_track(trak_meta_t *tm, const track_t *t) {
  memset(tm, 0, sizeof *tm);
  tm->track_id = t->track_id;
  tm->cls = t->cls;
  tm->width = t->width;
  tm->height = t->height;
  tm->codec = t->es.codec;
  tm->cpriv = t->es.cpriv;
  tm->cpriv_len = t->es.cpriv_len;
  tm->rate = t->es.rate;
  tm->channels = t->es.channels;
  tm->ac3_bsid = t->ac3_bsid;
  tm->ac3_bsmod = t->ac3_bsmod;
  tm->ac3_acmod = t->ac3_acmod;
  tm->ac3_lfeon = t->ac3_lfeon;
  tm->ac3_bitrate_code = t->ac3_bitrate_code;
  tm->truehd_format_info = t->truehd_format_info;
  tm->truehd_peak_data_rate = t->truehd_peak_data_rate;
  tm->dts_has_core = t->dts_has_core;
}

static void build_run_length_box(mp4buf_t *out, const track_t *t, const char *box_tag, int is_ctts) {
  mp4buf_t b;
  size_t cnt_pos;
  uint32_t entries = 0;
  uint32_t run_val = 0;
  uint32_t run_cnt = 0;
  int any = 0;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  cnt_pos = b.len;
  mb_u32(&b, 0);
  for (int i = 0; i < t->nsamp; i++) {
    uint32_t v = is_ctts ? (uint32_t)t->samp[i].cts_offset : t->samp[i].duration;
    if (v) any = 1;
    if (run_cnt && v == run_val) {
      run_cnt++;
      continue;
    }
    if (run_cnt) {
      mb_u32(&b, run_cnt);
      mb_u32(&b, run_val);
      entries++;
    }
    run_val = v;
    run_cnt = 1;
  }
  if (run_cnt) {
    mb_u32(&b, run_cnt);
    mb_u32(&b, run_val);
    entries++;
  }
  if (is_ctts && !any) {
    mp4buf_free(&b);
    return;
  }
  mb_patch_u32(&b, cnt_pos, entries);
  mb_box(out, box_tag, &b);
}

static void build_stts(mp4buf_t *out, const track_t *t) { build_run_length_box(out, t, "stts", 0); }

static void build_ctts(mp4buf_t *out, const track_t *t) { build_run_length_box(out, t, "ctts", 1); }

static void build_stsz(mp4buf_t *out, const track_t *t) {
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_u32(&b, 0);
  mb_u32(&b, (uint32_t)t->nsamp);
  for (int i = 0; i < t->nsamp; i++) mb_u32(&b, t->samp[i].size);
  mb_box(out, "stsz", &b);
}

static void build_stsc(mp4buf_t *out, const track_t *t, int ck) {
  mp4buf_t b;
  int any = t->nsamp || ck;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_u32(&b, any ? 1 : 0);
  if (any) {
    mb_u32(&b, 1);
    mb_u32(&b, 1);
    mb_u32(&b, 1);
  }
  mb_box(out, "stsc", &b);
}

static void build_stss(mp4buf_t *out, const track_t *t) {
  mp4buf_t b;
  size_t cnt_pos;
  uint32_t entries = 0;
  int any_nonkey = 0;
  for (int i = 0; i < t->nsamp; i++) {
    if (!t->samp[i].keyframe) any_nonkey = 1;
  }
  if (!any_nonkey) return;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  cnt_pos = b.len;
  mb_u32(&b, 0);
  for (int i = 0; i < t->nsamp; i++) {
    if (t->samp[i].keyframe) {
      mb_u32(&b, (uint32_t)(i + 1));
      entries++;
    }
  }
  mb_patch_u32(&b, cnt_pos, entries);
  mb_box(out, "stss", &b);
}

static void build_co64(mp4buf_t *out, const track_t *t) {
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_u32(&b, (uint32_t)t->nsamp);
  for (int i = 0; i < t->nsamp; i++) mb_u64(&b, t->samp[i].offset);
  mb_box(out, "co64", &b);
}

static void mb_zeros(mp4buf_t *b, size_t n) {
  static const unsigned char zero[4096];
  while (n) {
    size_t k = n < sizeof zero ? n : sizeof zero;
    mb_bytes(b, zero, k);
    n -= k;
  }
}

/* empty table with room for cap entries, filled in place later */
static void build_ck_table(mp4buf_t *out, const char *tag, const ck_tab_t *c, int stsz) {
  mp4buf_t b;
  if (!c->esz) return;
  memset(&b, 0, sizeof b);
  mb_u32(&b, 0);
  if (stsz) mb_u32(&b, 0);
  mb_u32(&b, 0);
  mb_zeros(&b, (size_t)c->cap * c->esz);
  mb_box(out, tag, &b);
}

static void build_stbl(mp4buf_t *out, const track_t *t, int ck) {
  mp4buf_t stbl;
  trak_meta_t tm;
  trak_meta_from_track(&tm, t);
  memset(&stbl, 0, sizeof stbl);
  trak_build_stsd(&stbl, &tm);
  if (ck) {
    build_ck_table(&stbl, "stts", &t->ck[CK_STTS], 0);
    build_ck_table(&stbl, "ctts", &t->ck[CK_CTTS], 0);
    build_ck_table(&stbl, "stss", &t->ck[CK_STSS], 0);
    build_stsc(&stbl, t, 1);
    build_ck_table(&stbl, "stsz", &t->ck[CK_STSZ], 1);
    build_ck_table(&stbl, t->ck_wide ? "co64" : "stco", &t->ck[CK_CO], 0);
  } else {
    build_stts(&stbl, t);
    if (t->cls == PID_VIDEO) build_ctts(&stbl, t);
    if (t->cls == PID_VIDEO || t->cls == PID_AUDIO) build_stss(&stbl, t);
    build_stsc(&stbl, t, 0);
    build_stsz(&stbl, t);
    build_co64(&stbl, t);
  }
  mb_box(out, "stbl", &stbl);
}

static void build_minf(mp4buf_t *out, const track_t *t, int ck) {
  mp4buf_t minf;
  memset(&minf, 0, sizeof minf);
  if (t->cls == PID_VIDEO)
    trak_build_vmhd(&minf);
  else if (t->cls == PID_AUDIO)
    trak_build_smhd(&minf);
  else
    build_nmhd(&minf);
  trak_build_dinf(&minf);
  build_stbl(&minf, t, ck);
  mb_box(out, "minf", &minf);
}

static void build_mdia(mp4buf_t *out, const track_t *t, uint32_t duration_ms, int ck) {
  mp4buf_t mdia;
  memset(&mdia, 0, sizeof mdia);
  build_mdhd(&mdia, t, duration_ms);
  trak_build_hdlr(&mdia, t->cls);
  build_minf(&mdia, t, ck);
  mb_box(out, "mdia", &mdia);
}

static uint32_t track_duration_ms(const track_t *t) {
  uint32_t d = 0;
  for (int i = 0; i < t->nsamp; i++) d += t->samp[i].duration;
  return d;
}

typedef struct {
  uint32_t empty_ms; /* leading gap vs movie origin */
  uint32_t skip_ms;  /* media arrival time, first presented sample */
} track_edit_t;

static track_edit_t track_edit(const track_t *t, int64_t origin_ms) {
  track_edit_t e = {0, 0};
  int64_t pres;
  if (!t->nsamp) return e;
  if (t->cls == PID_VIDEO && t->first_cts > 0) e.skip_ms = (uint32_t)t->first_cts;
  pres = t->first_ts_ms + e.skip_ms - origin_ms;
  if (pres > 0) e.empty_ms = (uint32_t)pres;
  return e;
}

static uint32_t track_movie_duration_ms(uint32_t d, const track_edit_t *e) {
  return e->empty_ms + (d > e->skip_ms ? d - e->skip_ms : 0);
}

static void build_edts(mp4buf_t *out, uint32_t media_dur, const track_edit_t *e) {
  mp4buf_t edts;
  mp4buf_t elst;
  uint32_t seg = media_dur > e->skip_ms ? media_dur - e->skip_ms : 0;
  if (!e->empty_ms && !e->skip_ms) return;
  memset(&edts, 0, sizeof edts);
  memset(&elst, 0, sizeof elst);
  mb_u8(&elst, 0);
  mb_u24(&elst, 0);
  mb_u32(&elst, e->empty_ms ? 2 : 1);
  if (e->empty_ms) {
    mb_u32(&elst, e->empty_ms);
    mb_u32(&elst, 0xFFFFFFFFu); /* media_time -1: empty edit */
    mb_u32(&elst, 0x00010000);
  }
  mb_u32(&elst, seg);
  mb_u32(&elst, e->skip_ms);
  mb_u32(&elst, 0x00010000);
  mb_box(&edts, "elst", &elst);
  mb_box(out, "edts", &edts);
}

static void build_trak(mp4buf_t *out, const track_t *t, int64_t origin_ms, int ck) {
  mp4buf_t trak;
  track_edit_t e = track_edit(t, origin_ms);
  uint32_t d = ck ? 0 : track_duration_ms(t); /* ck: patched in place */
  memset(&trak, 0, sizeof trak);
  build_tkhd(&trak, t, track_movie_duration_ms(d, &e));
  build_edts(&trak, d, &e);
  build_mdia(&trak, t, d, ck);
  mb_box(out, "trak", &trak);
}

static int build_moov(const mp4_t *m, mp4buf_t *top, int ck) {
  mp4buf_t moov;
  uint32_t movie_dur = 0;

  for (int i = 0; i < m->ntrk && !ck; i++) {
    track_edit_t e = track_edit(&m->trk[i], m->t0);
    uint32_t d = track_movie_duration_ms(track_duration_ms(&m->trk[i]), &e);
    if (d > movie_dur) movie_dur = d;
  }
  memset(&moov, 0, sizeof moov);
  build_mvhd(&moov, m->ntrk, movie_dur);
  for (int i = 0; i < m->ntrk; i++) build_trak(&moov, &m->trk[i], m->t0, ck);
  memset(top, 0, sizeof *top);
  mb_box(top, "moov", &moov);
  return top->p != NULL;
}

/* moov + trailing free header into region */
static int fits_region(const mp4buf_t *moov) {
  size_t rest;
  if (moov->len > MP4_MOOV_RESERVE) return 0;
  rest = MP4_MOOV_RESERVE - moov->len;
  return rest == 0 || rest >= 8;
}

static void write_region(mp4_t *m, const mp4buf_t *moov) {
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

static void ck_pwrite(mp4_t *m, const unsigned char *p, size_t n, size_t pos) {
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

static void ck_flush_tab(mp4_t *m, ck_tab_t *c) {
  unsigned char *img = m->ck_moov.p;
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
static void clear_stale(mp4_t *m, size_t from) {
  static const unsigned char zero[4096];
  for (size_t pos = from; pos < m->ck_moov.len && !m->err;) {
    size_t n = m->ck_moov.len - pos;
    if (n > sizeof zero) n = sizeof zero;
    ck_pwrite(m, zero, n, pos);
    pos += n;
  }
}

void p4_write_moov(mp4_t *m) {
  mp4buf_t top;
  unsigned char hdr[16];
  uint64_t mdat_size = *m->bytes - m->mdat_hdr_pos; /* before moov, or it'd count itself into mdat */

  if (!build_moov(m, &top, 0)) {
    m->err = 1;
    return;
  }
  put_be32(hdr, 1);
  memcpy(hdr + 4, "mdat", 4);
  put_be64(hdr + 8, mdat_size);
  if (fits_region(&top)) {
    write_region(m, &top);
    clear_stale(m, top.len + 8);
  } else {
    p4_wfd(m, top.p, top.len);
    if (!m->err && m->ckpt_done && pwrite(m->fd, "free", 4, (off_t)(m->moov_pos + 4)) != 4) m->err = 1; /* retire checkpoint */
  }
  mp4buf_free(&top);
  if (!m->err && pwrite(m->fd, hdr, sizeof hdr, (off_t)m->mdat_hdr_pos) != (ssize_t)sizeof hdr) m->err = 1;
}
