/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "box.h"

void mp4buf_free(mp4buf_t *b) { muxbuf_free(b); }

void mb_bytes(mp4buf_t *b, const void *data, size_t n) {
  if (!n) return;
  muxbuf_append(b, data, n, 256);
}

void mb_u8(mp4buf_t *b, unsigned v) {
  unsigned char c = (unsigned char)v;
  mb_bytes(b, &c, 1);
}

void mb_u16(mp4buf_t *b, unsigned v) {
  unsigned char c[2];
  c[0] = (unsigned char)(v >> 8);
  c[1] = (unsigned char)v;
  mb_bytes(b, c, 2);
}

void mb_u24(mp4buf_t *b, unsigned v) {
  unsigned char c[3];
  c[0] = (unsigned char)(v >> 16);
  c[1] = (unsigned char)(v >> 8);
  c[2] = (unsigned char)v;
  mb_bytes(b, c, 3);
}

void mb_u32(mp4buf_t *b, uint32_t v) {
  unsigned char c[4];
  c[0] = (unsigned char)(v >> 24);
  c[1] = (unsigned char)(v >> 16);
  c[2] = (unsigned char)(v >> 8);
  c[3] = (unsigned char)v;
  mb_bytes(b, c, 4);
}

void mb_u64(mp4buf_t *b, uint64_t v) {
  mb_u32(b, (uint32_t)(v >> 32));
  mb_u32(b, (uint32_t)v);
}

void mb_fourcc(mp4buf_t *b, const char fourcc[4]) {
  mb_bytes(b, fourcc, 4);
}

void mb_patch_u32(mp4buf_t *b, size_t pos, uint32_t v) {
  if (b->err || pos + 4 > b->len) return;
  b->p[pos] = (unsigned char)(v >> 24);
  b->p[pos + 1] = (unsigned char)(v >> 16);
  b->p[pos + 2] = (unsigned char)(v >> 8);
  b->p[pos + 3] = (unsigned char)v;
}

void mb_box(mp4buf_t *parent, const char fourcc[4], mp4buf_t *child) {
  if (child->err) parent->err = 1;
  mb_u32(parent, (uint32_t)(8 + child->len));
  mb_fourcc(parent, fourcc);
  mb_bytes(parent, child->p, child->len);
  mp4buf_free(child);
}

void put_matrix_unity(mp4buf_t *b) {
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

void put_desc_size(mp4buf_t *out, size_t len) {
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

void put_desc(mp4buf_t *out, unsigned tag, mp4buf_t *payload) {
  mb_u8(out, tag);
  put_desc_size(out, payload->len);
  if (payload->err) out->err = 1;
  mb_bytes(out, payload->p, payload->len);
  mp4buf_free(payload);
}

static const char *trak_entry_fourcc_for(codec_t codec) {
  switch (codec) {
    case CODEC_HEVC:  return "hvc1";
    case CODEC_VVC:   return "vvc1";
    case CODEC_LCEVC: return "lvc1";
    case CODEC_AC3:   return "ac-3";
    case CODEC_EAC3:  return "ec-3";
    case CODEC_H264:  return "avc1";
    case CODEC_OPUS:  return "Opus";
    default:          return "mp4a";
  }
}

void trak_build_tref(mp4buf_t *out, unsigned depends_on_track_id) {
  mp4buf_t tref, sbas;
  if (!depends_on_track_id) return;
  memset(&sbas, 0, sizeof sbas);
  mb_u32(&sbas, depends_on_track_id);
  memset(&tref, 0, sizeof tref);
  mb_box(&tref, "sbas", &sbas);
  mb_box(out, "tref", &tref);
}

void trak_build_hdlr(mp4buf_t *out, pid_class_t cls) {
  static const char vname[] = "VideoHandler";
  static const char aname[] = "SoundHandler";
  static const char tname[] = "TextHandler";
  const char *hty;
  const char *name;
  size_t namelen;
  if (cls == PID_VIDEO) { hty = "vide"; name = vname; namelen = sizeof vname; }
  else if (cls == PID_AUDIO) { hty = "soun"; name = aname; namelen = sizeof aname; }
  else { hty = "text"; name = tname; namelen = sizeof tname; }
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

void trak_build_vmhd(mp4buf_t *out) {
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

void trak_build_smhd(mp4buf_t *out) {
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u24(&b, 0);
  mb_u16(&b, 0);
  mb_u16(&b, 0);
  mb_box(out, "smhd", &b);
}

void trak_build_dinf(mp4buf_t *out) {
  mp4buf_t dref;
  mp4buf_t url;
  mp4buf_t dinf;
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

static void trak_build_esds(mp4buf_t *out, const trak_meta_t *t) {
  int is_aac = t->codec == CODEC_AAC || t->codec == CODEC_AAC_LATM;
  unsigned char oti = (unsigned char)(is_aac ? 0x40 : 0x6B);
  mp4buf_t esds;
  mp4buf_t es_desc;
  mp4buf_t dec_cfg;
  mp4buf_t sl;

  memset(&dec_cfg, 0, sizeof dec_cfg);
  mb_u8(&dec_cfg, oti);
  mb_u8(&dec_cfg, 0x15);
  mb_u24(&dec_cfg, 0);
  mb_u32(&dec_cfg, 0);
  mb_u32(&dec_cfg, 0);
  if (t->cpriv_len) {
    mp4buf_t dsi;
    memset(&dsi, 0, sizeof dsi);
    mb_bytes(&dsi, t->cpriv, t->cpriv_len);
    put_desc(&dec_cfg, 0x05, &dsi);
  }
  memset(&sl, 0, sizeof sl);
  mb_u8(&sl, 0x02);
  memset(&es_desc, 0, sizeof es_desc);
  mb_u16(&es_desc, t->track_id);
  mb_u8(&es_desc, 0);
  put_desc(&es_desc, 0x04, &dec_cfg);
  put_desc(&es_desc, 0x06, &sl);
  memset(&esds, 0, sizeof esds);
  mb_u8(&esds, 0);
  mb_u24(&esds, 0);
  put_desc(&esds, 0x03, &es_desc);
  mb_box(out, "esds", &esds);
}

static unsigned trak_ac3_fscod(unsigned rate) {
  if (rate == 48000) return 0;
  if (rate == 44100) return 1;
  if (rate == 32000) return 2;
  return 3;
}

static void trak_build_dac3(mp4buf_t *out, const trak_meta_t *t) {
  mp4buf_t b;
  unsigned fscod = trak_ac3_fscod(t->rate);
  memset(&b, 0, sizeof b);
  mb_u8(&b, (fscod << 6) | t->ac3_bsid << 1 | (t->ac3_bsmod >> 2));
  mb_u8(&b, (t->ac3_bsmod & 3) << 6 | t->ac3_acmod << 3 | t->ac3_lfeon << 2 | (t->ac3_bitrate_code >> 3));
  mb_u8(&b, (t->ac3_bitrate_code & 7) << 5);
  mb_box(out, "dac3", &b);
}

static void trak_build_dec3(mp4buf_t *out, const trak_meta_t *t) {
  mp4buf_t b;
  unsigned fscod = trak_ac3_fscod(t->rate);
  memset(&b, 0, sizeof b);
  mb_u16(&b, (t->ac3_bitrate_code & 0x1FFF) << 3);
  mb_u8(&b, (fscod << 6) | t->ac3_bsid << 1 | 0);
  mb_u8(&b, (unsigned)((t->ac3_bsmod << 5) | (t->ac3_acmod << 2) | (t->ac3_lfeon << 1)));
  mb_u8(&b, 0);
  mb_box(out, "dec3", &b);
}

static void trak_build_dops(mp4buf_t *out, const trak_meta_t *t) {
  mp4buf_t b;
  memset(&b, 0, sizeof b);
  mb_u8(&b, 0);
  mb_u8(&b, (unsigned char)t->channels);
  mb_u16(&b, 0);
  mb_u32(&b, 48000);
  mb_u16(&b, 0);
  mb_u8(&b, 0);
  mb_box(out, "dOps", &b);
}

static void trak_build_audio_entry(mp4buf_t *stsd, const trak_meta_t *t) {
  mp4buf_t entry;
  memset(&entry, 0, sizeof entry);
  for (int i = 0; i < 6; i++) mb_u8(&entry, 0);
  mb_u16(&entry, 1);
  mb_u32(&entry, 0);
  mb_u32(&entry, 0);
  mb_u16(&entry, t->channels);
  mb_u16(&entry, 16);
  mb_u16(&entry, 0);
  mb_u16(&entry, 0);
  mb_u32(&entry, (uint32_t)t->rate << 16);
  if (t->codec == CODEC_AC3)       trak_build_dac3(&entry, t);
  else if (t->codec == CODEC_EAC3) trak_build_dec3(&entry, t);
  else if (t->codec == CODEC_OPUS) trak_build_dops(&entry, t);
  else                             trak_build_esds(&entry, t);
  mb_box(stsd, trak_entry_fourcc_for(t->codec), &entry);
}

static void trak_build_video_entry(mp4buf_t *stsd, const trak_meta_t *t) {
  mp4buf_t entry;
  mp4buf_t cfgbox;
  const char *cfg_fourcc;
  if (t->codec == CODEC_HEVC) cfg_fourcc = "hvcC";
  else if (t->codec == CODEC_VVC) cfg_fourcc = "vvcC";
  else if (t->codec == CODEC_LCEVC) cfg_fourcc = "lvcC";
  else cfg_fourcc = "avcC";
  int i;
  memset(&entry, 0, sizeof entry);
  for (i = 0; i < 6; i++) mb_u8(&entry, 0);
  mb_u16(&entry, 1);
  mb_u16(&entry, 0);
  mb_u16(&entry, 0);
  mb_u32(&entry, 0);
  mb_u32(&entry, 0);
  mb_u32(&entry, 0);
  mb_u16(&entry, t->width);
  mb_u16(&entry, t->height);
  mb_u32(&entry, 0x00480000u);
  mb_u32(&entry, 0x00480000u);
  mb_u32(&entry, 0);
  mb_u16(&entry, 1);
  for (i = 0; i < 32; i++) mb_u8(&entry, 0);
  mb_u16(&entry, 0x0018);
  mb_u16(&entry, 0xFFFF);
  memset(&cfgbox, 0, sizeof cfgbox);
  mb_bytes(&cfgbox, t->cpriv, t->cpriv_len);
  mb_box(&entry, cfg_fourcc, &cfgbox);
  mb_box(stsd, trak_entry_fourcc_for(t->codec), &entry);
}

static void trak_build_text_entry(mp4buf_t *stsd) {
  mp4buf_t entry;
  mp4buf_t ftab;
  memset(&entry, 0, sizeof entry);
  for (int i = 0; i < 6; i++) mb_u8(&entry, 0);
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

void trak_build_stsd(mp4buf_t *out, const trak_meta_t *t) {
  mp4buf_t stsd;
  memset(&stsd, 0, sizeof stsd);
  mb_u8(&stsd, 0);
  mb_u24(&stsd, 0);
  mb_u32(&stsd, 1);
  if (t->cls == PID_VIDEO)      trak_build_video_entry(&stsd, t);
  else if (t->cls == PID_AUDIO) trak_build_audio_entry(&stsd, t);
  else                          trak_build_text_entry(&stsd);
  mb_box(out, "stsd", &stsd);
}
