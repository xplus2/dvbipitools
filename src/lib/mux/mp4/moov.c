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

static void put_matrix_unity(mp4buf_t *b) {
  mb_u32(b, 0x00010000);
  mb_u32(b, 0);
  mb_u32(b, 0);
  mb_u32(b, 0);
  mb_u32(b, 0x00010000);
  mb_u32(b, 0);
  mb_u32(b, 0);
  mb_u32(b, 0);
  mb_u32(b, 0x40000000);
}

void p4_write_ftyp_mdat_head(mp4_t *m) {
  mp4buf_t ftyp, top;
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

static void build_hdlr(mp4buf_t *out, pid_class_t cls) {
  static const char vname[] = "VideoHandler";
  static const char aname[] = "SoundHandler";
  static const char tname[] = "TextHandler";
  const char *hty = cls == PID_VIDEO ? "vide" : cls == PID_AUDIO ? "soun" : "text";
  const char *name = cls == PID_VIDEO ? vname : cls == PID_AUDIO ? aname : tname;
  size_t namelen = cls == PID_VIDEO ? sizeof vname : cls == PID_AUDIO ? sizeof aname : sizeof tname;
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_u32(&b, 0);
  mb_fourcc(&b, hty);
  mb_u32(&b, 0);
  mb_u32(&b, 0);
  mb_u32(&b, 0);
  mb_bytes(&b, name, namelen);
  mb_box(out, "hdlr", &b);
}

static void build_vmhd(mp4buf_t *out) {
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 1);
  mb_u16(&b, 0);
  mb_u16(&b, 0);
  mb_u16(&b, 0);
  mb_u16(&b, 0);
  mb_box(out, "vmhd", &b);
}

static void build_smhd(mp4buf_t *out) {
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_u16(&b, 0);
  mb_u16(&b, 0);
  mb_box(out, "smhd", &b);
}

static void build_nmhd(mp4buf_t *out) {
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_box(out, "nmhd", &b);
}

static void build_dinf(mp4buf_t *out) {
  mp4buf_t dref, url, dinf;
  memset(&url, 0, sizeof url);
  mb_u8(&url, 0);
  mb_u24(&url, 1);
  memset(&dref, 0, sizeof dref);
  mb_u8(&dref, 0);
  mb_u24(&dref, 0);
  mb_u32(&dref, 1);
  mb_box(&dref, "url ", &url);
  memset(&dinf, 0, sizeof dinf);
  mb_box(&dinf, "dref", &dref);
  mb_box(out, "dinf", &dinf);
}

static void put_desc_size(mp4buf_t *out, size_t len) {
  unsigned char tmp[4];
  int n = 0;
  do {
    tmp[n++] = (unsigned char)(len & 0x7F);
    len >>= 7;
  } while (len && n < 4);
  while (n > 1) {
    n--;
    mb_u8(out, tmp[n] | 0x80);
  }
  mb_u8(out, tmp[0]);
}

static void put_desc(mp4buf_t *out, unsigned tag, mp4buf_t *payload) {
  mb_u8(out, tag);
  put_desc_size(out, payload->len);
  if (payload->err)
    out->err = 1;
  mb_bytes(out, payload->p, payload->len);
  mp4buf_free(payload);
}

static void build_esds(mp4buf_t *out, const track_t *t) {
  int is_aac = t->es.codec == CODEC_AAC || t->es.codec == CODEC_AAC_LATM;
  unsigned char oti = (unsigned char)(is_aac ? 0x40 : 0x6B);
  mp4buf_t esds, es_desc, dec_cfg, sl;

  memset(&dec_cfg, 0, sizeof dec_cfg);
  mb_u8(&dec_cfg, oti);
  mb_u8(&dec_cfg, 0x15);
  mb_u24(&dec_cfg, 0);
  mb_u32(&dec_cfg, 0);
  mb_u32(&dec_cfg, 0);
  if (t->es.cpriv_len) {
    mp4buf_t dsi;
    memset(&dsi, 0, sizeof dsi);
    mb_bytes(&dsi, t->es.cpriv, t->es.cpriv_len);
    put_desc(&dec_cfg, 0x05, &dsi);
  }
  memset(&sl, 0, sizeof sl);
  mb_u8(&sl, 0x02);
  memset(&es_desc, 0, sizeof es_desc);
  mb_u16(&es_desc, (unsigned)t->track_id);
  mb_u8(&es_desc, 0);
  put_desc(&es_desc, 0x04, &dec_cfg);
  put_desc(&es_desc, 0x06, &sl);
  memset(&esds, 0, sizeof esds);
  mb_u8(&esds, 0);
  mb_u24(&esds, 0);
  put_desc(&esds, 0x03, &es_desc);
  mb_box(out, "esds", &esds);
}

static void build_dac3(mp4buf_t *out, const track_t *t) {
  mp4buf_t b;
  unsigned fscod = t->es.rate == 48000 ? 0 : t->es.rate == 44100 ? 1 : t->es.rate == 32000 ? 2 : 3;
  memset(&b, 0, sizeof b);
  mb_u8(&b, (fscod << 6) | t->ac3_bsid << 1 | (t->ac3_bsmod >> 2));
  mb_u8(&b, (t->ac3_bsmod & 3) << 6 | t->ac3_acmod << 3 | t->ac3_lfeon << 2 | (t->ac3_bitrate_code >> 3));
  mb_u8(&b, (t->ac3_bitrate_code & 7) << 5);
  mb_box(out, "dac3", &b);
}

static void build_dec3(mp4buf_t *out, const track_t *t) {
  mp4buf_t b;
  unsigned fscod = t->es.rate == 48000 ? 0 : t->es.rate == 44100 ? 1 : t->es.rate == 32000 ? 2 : 3;
  memset(&b, 0, sizeof b);
  mb_u16(&b, (t->ac3_bitrate_code & 0x1FFF) << 3);
  mb_u8(&b, (fscod << 6) | t->ac3_bsid << 1 | 0);
  mb_u8(&b, (unsigned)((t->ac3_bsmod << 5) | (t->ac3_acmod << 2) | (t->ac3_lfeon << 1)));
  mb_u8(&b, 0);
  mb_box(out, "dec3", &b);
}

static void build_audio_entry(mp4buf_t *stsd, const track_t *t) {
  mp4buf_t entry;
  int i;
  memset(&entry, 0, sizeof entry);
  for (i = 0; i < 6; i++) mb_u8(&entry, 0);
  mb_u16(&entry, 1);
  mb_u32(&entry, 0);
  mb_u32(&entry, 0);
  mb_u16(&entry, (unsigned)t->es.channels);
  mb_u16(&entry, 16);
  mb_u16(&entry, 0);
  mb_u16(&entry, 0);
  mb_u32(&entry, (uint32_t)t->es.rate << 16);
  if (t->es.codec == CODEC_AC3)
    build_dac3(&entry, t);
  else if (t->es.codec == CODEC_EAC3)
    build_dec3(&entry, t);
  else
    build_esds(&entry, t);
  mb_box(stsd, p4_entry_fourcc_for(t->es.codec), &entry);
}

static void build_video_entry(mp4buf_t *stsd, const track_t *t) {
  mp4buf_t entry, cfgbox;
  const char *cfg_fourcc = t->es.codec == CODEC_HEVC ? "hvcC" : t->es.codec == CODEC_VVC ? "vvcC" : "avcC";
  int i;
  memset(&entry, 0, sizeof entry);
  for (i = 0; i < 6; i++) mb_u8(&entry, 0);
  mb_u16(&entry, 1);
  mb_u16(&entry, 0);
  mb_u16(&entry, 0);
  mb_u32(&entry, 0);
  mb_u32(&entry, 0);
  mb_u32(&entry, 0);
  mb_u16(&entry, (unsigned)t->width);
  mb_u16(&entry, (unsigned)t->height);
  mb_u32(&entry, 0x00480000u);
  mb_u32(&entry, 0x00480000u);
  mb_u32(&entry, 0);
  mb_u16(&entry, 1);
  for (i = 0; i < 32; i++)
    mb_u8(&entry, 0);
  mb_u16(&entry, 0x0018);
  mb_u16(&entry, 0xFFFF);
  memset(&cfgbox, 0, sizeof cfgbox);
  mb_bytes(&cfgbox, t->es.cpriv, t->es.cpriv_len);
  mb_box(&entry, cfg_fourcc, &cfgbox);
  mb_box(stsd, p4_entry_fourcc_for(t->es.codec), &entry);
}

static void build_text_entry(mp4buf_t *stsd) {
  mp4buf_t entry, ftab;
  int i;
  memset(&entry, 0, sizeof entry);
  for (i = 0; i < 6; i++) mb_u8(&entry, 0);
  mb_u16(&entry, 1);
  mb_u32(&entry, 0); /* displayFlags */
  mb_u8(&entry, 1);  /* horizontal-justification: center */
  mb_u8(&entry, 0xFF); /* vertical-justification: bottom (-1) */
  mb_u32(&entry, 0); /* background-color-rgba: transparent */
  mb_u16(&entry, 0); /* default-text-box: top */
  mb_u16(&entry, 0); /* left */
  mb_u16(&entry, 0); /* bottom */
  mb_u16(&entry, 0); /* right */
  mb_u16(&entry, 0); /* default-style: startChar */
  mb_u16(&entry, 0); /* endChar */
  mb_u16(&entry, 1); /* font-ID */
  mb_u8(&entry, 0);  /* face-style-flags */
  mb_u8(&entry, 18); /* font-size */
  mb_u32(&entry, 0xFFFFFFFFu); /* text-color-rgba: opaque white */
  memset(&ftab, 0, sizeof ftab);
  mb_u16(&ftab, 1);
  mb_u16(&ftab, 1); /* font-ID */
  mb_u8(&ftab, 0);  /* font-name-length */
  mb_box(&entry, "ftab", &ftab);
  mb_box(stsd, "tx3g", &entry);
}

static void build_stsd(mp4buf_t *out, const track_t *t) {
  mp4buf_t stsd;
  memset(&stsd, 0, sizeof stsd);
  mb_u8(&stsd, 0);
  mb_u24(&stsd, 0);
  mb_u32(&stsd, 1);
  if (t->cls == PID_VIDEO)
    build_video_entry(&stsd, t);
  else if (t->cls == PID_AUDIO)
    build_audio_entry(&stsd, t);
  else
    build_text_entry(&stsd);
  mb_box(out, "stsd", &stsd);
}

static void build_stts(mp4buf_t *out, const track_t *t) {
  mp4buf_t b;
  size_t cnt_pos;
  uint32_t entries = 0, run_dur = 0, run_cnt = 0;
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
  uint32_t entries = 0, run_off = 0, run_cnt = 0;
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
  memset(&stbl, 0, sizeof stbl);
  build_stsd(&stbl, t);
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
    build_vmhd(&minf);
  else if (t->cls == PID_AUDIO)
    build_smhd(&minf);
  else
    build_nmhd(&minf);
  build_dinf(&minf);
  build_stbl(&minf, t);
  mb_box(out, "minf", &minf);
}

static void build_mdia(mp4buf_t *out, const track_t *t, uint32_t duration_ms) {
  mp4buf_t mdia;
  memset(&mdia, 0, sizeof mdia);
  build_mdhd(&mdia, t, duration_ms);
  build_hdlr(&mdia, t->cls);
  build_minf(&mdia, t);
  mb_box(out, "mdia", &mdia);
}

static uint32_t track_duration_ms(const track_t *t) {
  uint32_t d = 0;
  for (int i = 0; i < t->nsamp; i++)
    d += t->samp[i].duration;
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
  mp4buf_t moov, top;
  unsigned char patch[8];
  uint64_t mdat_size;
  uint32_t movie_dur = 0;

  for (int i = 0; i < m->ntrk; i++) {
    uint32_t d = track_duration_ms(&m->trk[i]);
    if (d > movie_dur) movie_dur = d;
  }
  mdat_size = *m->bytes - m->mdat_hdr_pos; /* before moov, or it'd count itself into mdat */
  put_be64(patch, mdat_size);
  if (pwrite(m->fd, patch, sizeof patch, (off_t)(m->mdat_hdr_pos + 8)) != (ssize_t)sizeof patch)
    m->err = 1;

  memset(&moov, 0, sizeof moov);
  build_mvhd(&moov, m->ntrk, movie_dur);
  for (int i = 0; i < m->ntrk; i++) build_trak(&moov, &m->trk[i]);
  memset(&top, 0, sizeof top);
  mb_box(&top, "moov", &moov);
  p4_wfd(m, top.p, top.len);
  mp4buf_free(&top);
}
