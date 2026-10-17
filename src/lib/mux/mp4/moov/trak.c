/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/helper/log.h"
#include "lib/mux/mp4/priv.h"

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

void trak_meta_from_track(trak_meta_t *tm, const track_t *t) {
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

track_edit_t track_edit(const track_t *t, int64_t origin_ms) {
  track_edit_t e = {0, 0};
  int64_t pres;
  if (!t->nsamp) return e;
  if (t->cls == PID_VIDEO && t->first_cts > 0) e.skip_ms = (uint32_t)t->first_cts;
  pres = t->first_ts_ms + e.skip_ms - origin_ms;
  if (pres > 0) e.empty_ms = (uint32_t)pres;
  return e;
}

uint32_t track_movie_duration_ms(uint32_t d, const track_edit_t *e) {
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

int build_moov(const mp4_t *m, mp4buf_t *top, int ck) {
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
