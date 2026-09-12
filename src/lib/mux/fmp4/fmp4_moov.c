/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "fmp4_int.h"

#include <string.h>

void build_mvhd(mp4buf_t *out, int ntrk) {
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_u32(&b, 0);           /* creation_time */
  mb_u32(&b, 0);           /* modification_time */
  mb_u32(&b, 1000);        /* timescale */
  mb_u32(&b, 0xFFFFFFFFu); /* duration: unknown, fragmented */
  mb_u32(&b, 0x00010000);  /* rate 1.0 */
  mb_u16(&b, 0x0100);      /* volume 1.0 */
  mb_u16(&b, 0);
  mb_u32(&b, 0);
  mb_u32(&b, 0);
  put_matrix_unity(&b);
  for (int i = 0; i < 6; i++) mb_u32(&b, 0); /* predefined */
  mb_u32(&b, (uint32_t)(ntrk + 1));
  mb_box(out, "mvhd", &b);
}

static void build_tkhd(mp4buf_t *out, const fmp4_trk_t *t) {
  mp4buf_t b;
  int is_video = (t->cfg.width > 0 || t->cfg.height > 0);
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0x000007); /* enabled | in_movie | in_preview */
  mb_u32(&b, 0);
  mb_u32(&b, 0);
  mb_u32(&b, t->cfg.track_id);
  mb_u32(&b, 0); /* reserved */
  mb_u32(&b, 0xFFFFFFFFu);
  mb_u32(&b, 0);
  mb_u32(&b, 0);
  mb_u16(&b, 0); /* layer */
  mb_u16(&b, 0); /* alternate_group */
  mb_u16(&b, (unsigned)(is_video ? 0 : 0x0100));
  mb_u16(&b, 0);
  put_matrix_unity(&b);
  mb_u32(&b, (uint32_t)t->cfg.width << 16);
  mb_u32(&b, (uint32_t)t->cfg.height << 16);
  mb_box(out, "tkhd", &b);
}

static void build_mdhd(mp4buf_t *out, const fmp4_trk_t *t) {
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_u32(&b, 0);
  mb_u32(&b, 0);
  mb_u32(&b, t->cfg.timescale);
  mb_u32(&b, 0xFFFFFFFFu);
  mb_u16(&b, 0x55C4); /* language "und" */
  mb_u16(&b, 0);
  mb_box(out, "mdhd", &b);
}

static int codec_is_audio(codec_t c) {
  return c == CODEC_AAC || c == CODEC_AAC_LATM || c == CODEC_AC3 || c == CODEC_EAC3 || c == CODEC_MP2A || c == CODEC_OPUS;
}

static void trak_meta_from_trk(trak_meta_t *tm, const fmp4_trk_t *t) {
  memset(tm, 0, sizeof *tm);
  tm->track_id = t->cfg.track_id;
  tm->cls = codec_is_audio(t->cfg.codec) ? PID_AUDIO : PID_VIDEO;
  tm->width = t->cfg.width;
  tm->height = t->cfg.height;
  tm->codec = t->cfg.codec;
  tm->cpriv = t->cfg.cpriv;
  tm->cpriv_len = t->cfg.cpriv_len;
  tm->rate = t->cfg.rate;
  tm->channels = t->cfg.channels;
  tm->ac3_bsid = t->cfg.ac3_bsid;
  tm->ac3_bsmod = t->cfg.ac3_bsmod;
  tm->ac3_acmod = t->cfg.ac3_acmod;
  tm->ac3_lfeon = t->cfg.ac3_lfeon;
  tm->ac3_bitrate_code = t->cfg.ac3_bitrate_code;
}

static void build_empty_table(mp4buf_t *out, const char fourcc[4]) {
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_u32(&b, 0); /* entry_count */
  mb_box(out, fourcc, &b);
}

static void build_stbl(mp4buf_t *out, const fmp4_trk_t *t) {
  mp4buf_t stsz, stbl;
  trak_meta_t tm;
  trak_meta_from_trk(&tm, t);
  memset(&stbl, 0, sizeof stbl);
  trak_build_stsd(&stbl, &tm);
  build_empty_table(&stbl, "stts");
  build_empty_table(&stbl, "stsc");
  memset(&stsz, 0, sizeof stsz);
  mb_u8(&stsz, 0);
  mb_u24(&stsz, 0);
  mb_u32(&stsz, 0); /* sample_size */
  mb_u32(&stsz, 0); /* sample_count */
  mb_box(&stbl, "stsz", &stsz);
  build_empty_table(&stbl, "stco");
  mb_box(out, "stbl", &stbl);
}

static void build_minf(mp4buf_t *out, const fmp4_trk_t *t) {
  mp4buf_t minf;
  memset(&minf, 0, sizeof minf);
  if (codec_is_audio(t->cfg.codec))   trak_build_smhd(&minf);
  else                                trak_build_vmhd(&minf);
  trak_build_dinf(&minf);
  build_stbl(&minf, t);
  mb_box(out, "minf", &minf);
}

static void build_mdia(mp4buf_t *out, const fmp4_trk_t *t) {
  mp4buf_t mdia;
  trak_meta_t tm;
  trak_meta_from_trk(&tm, t);
  memset(&mdia, 0, sizeof mdia);
  build_mdhd(&mdia, t);
  trak_build_hdlr(&mdia, tm.cls);
  build_minf(&mdia, t);
  mb_box(out, "mdia", &mdia);
}

void build_trak(mp4buf_t *out, const fmp4_trk_t *t) {
  mp4buf_t trak;
  memset(&trak, 0, sizeof trak);
  build_tkhd(&trak, t);
  trak_build_tref(&trak, t->cfg.depends_on_track_id);
  build_mdia(&trak, t);
  mb_box(out, "trak", &trak);
}

void build_mvex(mp4buf_t *out, const fmp4_mux_t *m) {
  mp4buf_t mvex;
  memset(&mvex, 0, sizeof mvex);
  for (int i = 0; i < m->ntrk; i++) {
    mp4buf_t trex;
    memset(&trex, 0, sizeof trex);
    mb_u8(&trex, 0);
    mb_u24(&trex, 0);
    mb_u32(&trex, m->trk[i].cfg.track_id);
    mb_u32(&trex, 1); /* default_sample_description_index */
    mb_u32(&trex, 0);
    mb_u32(&trex, 0);
    mb_u32(&trex, 0);
    mb_box(&mvex, "trex", &trex);
  }
  mb_box(out, "mvex", &mvex);
}
