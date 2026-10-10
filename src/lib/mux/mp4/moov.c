/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/helper/log.h"
#include "priv.h"

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

static void build_stsc(mp4buf_t *out, const track_t *t) {
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_u32(&b, t->nsamp ? 1 : 0);
  if (t->nsamp) {
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

static void build_stbl(mp4buf_t *out, const track_t *t) {
  mp4buf_t stbl;
  trak_meta_t tm;
  trak_meta_from_track(&tm, t);
  memset(&stbl, 0, sizeof stbl);
  trak_build_stsd(&stbl, &tm);
  build_stts(&stbl, t);
  if (t->cls == PID_VIDEO) build_ctts(&stbl, t);
  if (t->cls == PID_VIDEO || t->cls == PID_AUDIO) build_stss(&stbl, t);
  build_stsc(&stbl, t);
  build_stsz(&stbl, t);
  build_co64(&stbl, t);
  mb_box(out, "stbl", &stbl);
}

static void build_minf(mp4buf_t *out, const track_t *t) {
  mp4buf_t minf;
  memset(&minf, 0, sizeof minf);
  if (t->cls == PID_VIDEO)
    trak_build_vmhd(&minf);
  else if (t->cls == PID_AUDIO)
    trak_build_smhd(&minf);
  else
    build_nmhd(&minf);
  trak_build_dinf(&minf);
  build_stbl(&minf, t);
  mb_box(out, "minf", &minf);
}

static void build_mdia(mp4buf_t *out, const track_t *t, uint32_t duration_ms) {
  mp4buf_t mdia;
  memset(&mdia, 0, sizeof mdia);
  build_mdhd(&mdia, t, duration_ms);
  trak_build_hdlr(&mdia, t->cls);
  build_minf(&mdia, t);
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

static uint32_t track_movie_duration_ms(const track_t *t, const track_edit_t *e) {
  uint32_t d = track_duration_ms(t);
  return e->empty_ms + (d > e->skip_ms ? d - e->skip_ms : 0);
}

static void build_edts(mp4buf_t *out, const track_t *t, const track_edit_t *e) {
  mp4buf_t edts;
  mp4buf_t elst;
  uint32_t media_dur = track_duration_ms(t);
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

static void build_trak(mp4buf_t *out, const track_t *t, int64_t origin_ms) {
  mp4buf_t trak;
  track_edit_t e = track_edit(t, origin_ms);
  memset(&trak, 0, sizeof trak);
  build_tkhd(&trak, t, track_movie_duration_ms(t, &e));
  build_edts(&trak, t, &e);
  build_mdia(&trak, t, track_duration_ms(t));
  mb_box(out, "trak", &trak);
}

static int build_moov(const mp4_t *m, mp4buf_t *top) {
  mp4buf_t moov;
  uint32_t movie_dur = 0;

  for (int i = 0; i < m->ntrk; i++) {
    track_edit_t e = track_edit(&m->trk[i], m->t0);
    uint32_t d = track_movie_duration_ms(&m->trk[i], &e);
    if (d > movie_dur) movie_dur = d;
  }
  memset(&moov, 0, sizeof moov);
  build_mvhd(&moov, m->ntrk, movie_dur);
  for (int i = 0; i < m->ntrk; i++) build_trak(&moov, &m->trk[i], m->t0);
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

void p4_checkpoint(mp4_t *m) {
  mp4buf_t top;

  if (!m->started || m->err || m->ckpt_full) return;
  for (int i = 0; i < m->ntrk; i++) {
    track_t *t = &m->trk[i];
    if (t->cls == PID_VIDEO && t->prev_dur_slot && !*t->prev_dur_slot) *t->prev_dur_slot = t->last_dur ? t->last_dur : 1; /* next frame overwrites */
  }
  if (!build_moov(m, &top)) {
    m->err = 1;
    return;
  }
  if (!fits_region(&top)) {
    log_line("mp4: index outgrew reserved region, no further crash checkpoints");
    m->ckpt_full = 1;
  } else {
    write_region(m, &top);
    m->ckpt_done = 1;
  }
  mp4buf_free(&top);
}

void p4_write_moov(mp4_t *m) {
  mp4buf_t top;
  unsigned char hdr[16];
  uint64_t mdat_size = *m->bytes - m->mdat_hdr_pos; /* before moov, or it'd count itself into mdat */

  if (!build_moov(m, &top)) {
    m->err = 1;
    return;
  }
  put_be32(hdr, 1);
  memcpy(hdr + 4, "mdat", 4);
  put_be64(hdr + 8, mdat_size);
  if (fits_region(&top)) {
    write_region(m, &top);
  } else {
    p4_wfd(m, top.p, top.len);
    if (!m->err && m->ckpt_done && pwrite(m->fd, "free", 4, (off_t)(m->moov_pos + 4)) != 4) m->err = 1; /* retire checkpoint */
  }
  mp4buf_free(&top);
  if (!m->err && pwrite(m->fd, hdr, sizeof hdr, (off_t)m->mdat_hdr_pos) != (ssize_t)sizeof hdr) m->err = 1;
}
