/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "fmp4_int.h"

#include <string.h>

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
  for (int i = 0; i < 6; i++)
    mb_u32(&b, 0); /* pre_defined */
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
  return c == CODEC_AAC || c == CODEC_AAC_LATM || c == CODEC_AC3 || c == CODEC_EAC3 || c == CODEC_MP2A;
}

static void build_hdlr(mp4buf_t *out, int is_audio) {
  static const char vname[] = "VideoHandler";
  static const char aname[] = "SoundHandler";
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_u32(&b, 0); /* pre_defined */
  mb_fourcc(&b, is_audio ? "soun" : "vide");
  mb_u32(&b, 0);
  mb_u32(&b, 0);
  mb_u32(&b, 0); /* reserved */
  if (is_audio)
    mb_bytes(&b, aname, sizeof aname);
  else
    mb_bytes(&b, vname, sizeof vname);
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
  mb_u16(&b, 0); /* balance */
  mb_u16(&b, 0); /* reserved */
  mb_box(out, "smhd", &b);
}

static void build_dinf(mp4buf_t *out) {
  mp4buf_t dref, url, dinf;
  memset(&url, 0, sizeof url);
  mb_u8(&url, 0);
  mb_u24(&url, 1); /* self-contained */
  memset(&dref, 0, sizeof dref);
  mb_u8(&dref, 0);
  mb_u24(&dref, 0);
  mb_u32(&dref, 1);
  mb_box(&dref, "url ", &url);
  memset(&dinf, 0, sizeof dinf);
  mb_box(&dinf, "dref", &dref);
  mb_box(out, "dinf", &dinf);
}

/* MPEG-4 descriptor length: base-128, MSB=continuation, ISO/IEC 14496-1 8.3.3 */
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

static void build_esds(mp4buf_t *out, const fmp4_trk_t *t) {
  int is_aac = t->cfg.codec == CODEC_AAC || t->cfg.codec == CODEC_AAC_LATM;
  unsigned char oti = (unsigned char)(is_aac ? 0x40 : 0x6B); /* MPEG-4 Audio vs MPEG-1 Audio */
  mp4buf_t esds, es_desc, dec_cfg, sl;

  memset(&dec_cfg, 0, sizeof dec_cfg);
  mb_u8(&dec_cfg, oti);
  mb_u8(&dec_cfg, 0x15); /* streamType=5 (AudioStream) << 2 | upStream=0 | reserved=1 */
  mb_u24(&dec_cfg, 0);   /* bufferSizeDB */
  mb_u32(&dec_cfg, 0);   /* maxBitrate */
  mb_u32(&dec_cfg, 0);   /* avgBitrate */
  if (t->cfg.cpriv_len) {
    mp4buf_t dsi;
    memset(&dsi, 0, sizeof dsi);
    mb_bytes(&dsi, t->cfg.cpriv, t->cfg.cpriv_len);
    put_desc(&dec_cfg, 0x05, &dsi); /* DecoderSpecificInfo */
  }
  memset(&sl, 0, sizeof sl);
  mb_u8(&sl, 0x02); /* SLConfigDescriptor, predefined = MP4 */
  memset(&es_desc, 0, sizeof es_desc);
  mb_u16(&es_desc, t->cfg.track_id); /* ES_ID */
  mb_u8(&es_desc, 0);                          /* flags: no dependency/URL/OCR */
  put_desc(&es_desc, 0x04, &dec_cfg);          /* DecoderConfigDescriptor */
  put_desc(&es_desc, 0x06, &sl);               /* SLConfigDescriptor */
  memset(&esds, 0, sizeof esds);
  mb_u8(&esds, 0);
  mb_u24(&esds, 0);
  put_desc(&esds, 0x03, &es_desc); /* ES_Descriptor */
  mb_box(out, "esds", &esds);
}

static void build_dac3(mp4buf_t *out, const fmp4_trk_t *t) {
  mp4buf_t b;
  unsigned fscod = t->cfg.rate == 48000 ? 0 : t->cfg.rate == 44100 ? 1 : t->cfg.rate == 32000 ? 2 : 3;
  memset(&b, 0, sizeof b);
  mb_u8(&b, (fscod << 6) | t->cfg.ac3_bsid << 1 | (t->cfg.ac3_bsmod >> 2));
  mb_u8(&b, (t->cfg.ac3_bsmod & 3) << 6 | t->cfg.ac3_acmod << 3 | t->cfg.ac3_lfeon << 2 | (t->cfg.ac3_bitrate_code >> 3));
  mb_u8(&b, (t->cfg.ac3_bitrate_code & 7) << 5);
  mb_box(out, "dac3", &b);
}

static void build_dec3(mp4buf_t *out, const fmp4_trk_t *t) {
  mp4buf_t b;
  unsigned fscod = t->cfg.rate == 48000 ? 0 : t->cfg.rate == 44100 ? 1 : t->cfg.rate == 32000 ? 2 : 3;
  memset(&b, 0, sizeof b);
  mb_u16(&b, (t->cfg.ac3_bitrate_code & 0x1FFF) << 3); /* data_rate:13, num_ind_sub-1:3=0 */
  mb_u8(&b, (fscod << 6) | t->cfg.ac3_bsid << 1 | 0);  /* fscod:2 bsid:5 asvc:1 */
  mb_u8(&b, (unsigned)((t->cfg.ac3_bsmod << 5) | (t->cfg.ac3_acmod << 2) | (t->cfg.ac3_lfeon << 1)));
  mb_u8(&b, 0); /* num_dep_sub:4=0, reserved:1, pad */
  mb_box(out, "dec3", &b);
}

static void build_stsd(mp4buf_t *out, const fmp4_trk_t *t) {
  mp4buf_t entry, stsd;
  int i;

  if (codec_is_audio(t->cfg.codec)) {
    const char *entry_fourcc = t->cfg.codec == CODEC_AC3 ? "ac-3" : t->cfg.codec == CODEC_EAC3 ? "ec-3" : "mp4a";
    memset(&entry, 0, sizeof entry);
    for (i = 0; i < 6; i++)
      mb_u8(&entry, 0); /* reserved */
    mb_u16(&entry, 1);  /* data_reference_index */
    mb_u32(&entry, 0);  /* reserved */
    mb_u32(&entry, 0);
    mb_u16(&entry, t->cfg.channels);
    mb_u16(&entry, 16); /* samplesize */
    mb_u16(&entry, 0);  /* pre_defined */
    mb_u16(&entry, 0);  /* reserved */
    mb_u32(&entry, (uint32_t)t->cfg.rate << 16);
    if (t->cfg.codec == CODEC_AC3)
      build_dac3(&entry, t);
    else if (t->cfg.codec == CODEC_EAC3)
      build_dec3(&entry, t);
    else
      build_esds(&entry, t);

    memset(&stsd, 0, sizeof stsd);
    mb_u8(&stsd, 0);
    mb_u24(&stsd, 0);
    mb_u32(&stsd, 1);
    mb_box(&stsd, entry_fourcc, &entry);
    mb_box(out, "stsd", &stsd);
    return;
  }

  {
    mp4buf_t cfgbox;
    const char *entry_fourcc = (t->cfg.codec == CODEC_HEVC) ? "hvc1" : "avc1";
    const char *cfg_fourcc = (t->cfg.codec == CODEC_HEVC) ? "hvcC" : "avcC";

    memset(&entry, 0, sizeof entry);
    for (i = 0; i < 6; i++) mb_u8(&entry, 0); /* reserved */
    mb_u16(&entry, 1);  /* data_reference_index */
    mb_u16(&entry, 0);
    mb_u16(&entry, 0);
    mb_u32(&entry, 0);
    mb_u32(&entry, 0);
    mb_u32(&entry, 0); /* pre_defined */
    mb_u16(&entry, t->cfg.width);
    mb_u16(&entry, t->cfg.height);
    mb_u32(&entry, 0x00480000u); /* horizresolution 72dpi */
    mb_u32(&entry, 0x00480000u); /* vertresolution 72dpi */
    mb_u32(&entry, 0);
    mb_u16(&entry, 1); /* frame_count */
    for (i = 0; i < 32; i++)
      mb_u8(&entry, 0); /* compressorname, len-prefix 0 = empty */
    mb_u16(&entry, 0x0018); /* depth */
    mb_u16(&entry, 0xFFFF); /* pre_defined = -1 */

    memset(&cfgbox, 0, sizeof cfgbox);
    mb_bytes(&cfgbox, t->cfg.cpriv, t->cfg.cpriv_len);
    mb_box(&entry, cfg_fourcc, &cfgbox);

    memset(&stsd, 0, sizeof stsd);
    mb_u8(&stsd, 0);
    mb_u24(&stsd, 0);
    mb_u32(&stsd, 1);
    mb_box(&stsd, entry_fourcc, &entry);
    mb_box(out, "stsd", &stsd);
  }
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
  memset(&stbl, 0, sizeof stbl);
  build_stsd(&stbl, t);
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
  if (codec_is_audio(t->cfg.codec))
    build_smhd(&minf);
  else
    build_vmhd(&minf);
  build_dinf(&minf);
  build_stbl(&minf, t);
  mb_box(out, "minf", &minf);
}

static void build_mdia(mp4buf_t *out, const fmp4_trk_t *t) {
  mp4buf_t mdia;
  memset(&mdia, 0, sizeof mdia);
  build_mdhd(&mdia, t);
  build_hdlr(&mdia, codec_is_audio(t->cfg.codec));
  build_minf(&mdia, t);
  mb_box(out, "mdia", &mdia);
}

void build_trak(mp4buf_t *out, const fmp4_trk_t *t) {
  mp4buf_t trak;
  memset(&trak, 0, sizeof trak);
  build_tkhd(&trak, t);
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
