/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>
#include <sys/types.h>
#include <unistd.h>

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
  m->mdat_hdr_pos = *m->bytes;
  put_be32(mdat_hdr, 1);
  memcpy(mdat_hdr + 4, "mdat", 4);
  memset(mdat_hdr + 8, 0, 8); /* largesize, patched at close */
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
}

static void build_stts(mp4buf_t *out, const track_t *t) {
  mp4buf_t b;
  size_t cnt_pos;
  uint32_t entries = 0;
  uint32_t run_dur = 0;
  uint32_t run_cnt = 0;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  cnt_pos = b.len;
  mb_u32(&b, 0);
  for (int i = 0; i < t->nsamp; i++) {
    uint32_t d = t->samp[i].duration;
    if (run_cnt && d == run_dur) {
      run_cnt++;
      continue;
    }
    if (run_cnt) {
      mb_u32(&b, run_cnt);
      mb_u32(&b, run_dur);
      entries++;
    }
    run_dur = d;
    run_cnt = 1;
  }
  if (run_cnt) {
    mb_u32(&b, run_cnt);
    mb_u32(&b, run_dur);
    entries++;
  }
  mb_patch_u32(&b, cnt_pos, entries);
  mb_box(out, "stts", &b);
}

static void build_ctts(mp4buf_t *out, const track_t *t) {
  mp4buf_t b;
  size_t cnt_pos;
  uint32_t entries = 0;
  uint32_t run_off = 0;
  uint32_t run_cnt = 0;
  int any = 0;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  cnt_pos = b.len;
  mb_u32(&b, 0);
  for (int i = 0; i < t->nsamp; i++) {
    uint32_t o = (uint32_t)t->samp[i].cts_offset;
    if (o) any = 1;
    if (run_cnt && o == run_off) {
      run_cnt++;
      continue;
    }
    if (run_cnt) {
      mb_u32(&b, run_cnt);
      mb_u32(&b, run_off);
      entries++;
    }
    run_off = o;
    run_cnt = 1;
  }
  if (run_cnt) {
    mb_u32(&b, run_cnt);
    mb_u32(&b, run_off);
    entries++;
  }
  if (!any) {
    mp4buf_free(&b);
    return;
  }
  mb_patch_u32(&b, cnt_pos, entries);
  mb_box(out, "ctts", &b);
}

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
  for (int i = 0; i < t->nsamp; i++) if (!t->samp[i].keyframe) any_nonkey = 1;
  if (!any_nonkey) return;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  cnt_pos = b.len;
  mb_u32(&b, 0);
  for (int i = 0; i < t->nsamp; i++) if (t->samp[i].keyframe) {
    mb_u32(&b, (uint32_t)(i + 1));
    entries++;
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
  if (t->cls == PID_VIDEO) {
    build_ctts(&stbl, t);
    build_stss(&stbl, t);
  }
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

static void build_trak(mp4buf_t *out, const track_t *t) {
  mp4buf_t trak;
  uint32_t dur = track_duration_ms(t);
  memset(&trak, 0, sizeof trak);
  build_tkhd(&trak, t, dur);
  build_mdia(&trak, t, dur);
  mb_box(out, "trak", &trak);
}

void p4_write_moov(mp4_t *m) {
  mp4buf_t moov;
  mp4buf_t top;
  unsigned char patch[8];
  uint64_t mdat_size;
  uint32_t movie_dur = 0;

  for (int i = 0; i < m->ntrk; i++) {
    uint32_t d = track_duration_ms(&m->trk[i]);
    if (d > movie_dur) movie_dur = d;
  }
  mdat_size = *m->bytes - m->mdat_hdr_pos; /* before moov, or it'd count itself into mdat */
  put_be64(patch, mdat_size);
  if (pwrite(m->fd, patch, sizeof patch, (off_t)(m->mdat_hdr_pos + 8)) != (ssize_t)sizeof patch) m->err = 1;
  memset(&moov, 0, sizeof moov);
  build_mvhd(&moov, m->ntrk, movie_dur);
  for (int i = 0; i < m->ntrk; i++) build_trak(&moov, &m->trk[i]);
  memset(&top, 0, sizeof top);
  mb_box(&top, "moov", &moov);
  p4_wfd(m, top.p, top.len);
  mp4buf_free(&top);
}
