/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "pmtbuild.h"

#include <string.h>

#include "lib/mux/psi_build.h"

static int is_video_codec(codec_t c) {
  return c == CODEC_MPEG2V || c == CODEC_H264 || c == CODEC_HEVC || c == CODEC_VVC || c == CODEC_AV1 || c == CODEC_LCEVC;
}

unsigned esbuild_assign_pids(const codec_t *codecs, unsigned n, esbuild_es_t *out) {
  if (n > ESBUILD_MAX_ES) n = ESBUILD_MAX_ES;
  for (unsigned i = 0; i < n; i++) {
    out[i].codec = codecs[i];
    out[i].pid = ESBUILD_FIRST_ES_PID + i;
  }
  return n;
}

size_t esbuild_build_pat(unsigned tsid, unsigned version, unsigned program_number, unsigned char *out, size_t cap) {
  return psi_build_pat(tsid, version, program_number, ESBUILD_PMT_PID, out, cap);
}

/* stream_type per codec, shared fact about codec/stream_type, no per-tool logic */
static unsigned stream_type_for(codec_t c) {
  switch (c) {
    case CODEC_MPEG2V: return 0x02;
    case CODEC_H264: return 0x1B;
    case CODEC_HEVC: return 0x24;
    case CODEC_VVC: return 0x33;
    case CODEC_AV1: return 0x06;
    case CODEC_MP2A: return 0x03;
    case CODEC_AAC: return 0x0F;
    case CODEC_AAC_LATM: return 0x11;
    case CODEC_AC3: return 0x81;
    case CODEC_EAC3: return 0x87;
    case CODEC_OPUS: return 0x06;
    case CODEC_LCEVC: return 0x36;
    case CODEC_DTS: return 0x06;
    case CODEC_DTS_HD: return 0x06;
    case CODEC_DTS_HD_MA: return 0x06;
    case CODEC_TRUEHD: return 0x83;
    case CODEC_AC4: return 0x06;
    case CODEC_NONE: return 0;
  }
  return 0;
}

static size_t put_desc_hdr(unsigned char *out, unsigned tag, unsigned len) {
  out[0] = (unsigned char)tag;
  out[1] = (unsigned char)len;
  return 2;
}

static size_t build_registration_desc(unsigned char *out, const char id[4]) {
  size_t n = put_desc_hdr(out, 0x05, 4);
  memcpy(out + n, id, 4);
  return n + 4;
}

static size_t build_ext_desc_ac4(unsigned char *out) {
  size_t n = put_desc_hdr(out, 0x7F, 1);
  out[n++] = 0x15;
  return n;
}

/* mini DTS-HD_descriptor, EN 300 468 annex G tab G.6f/G.9: one substream, one asset,
   asset_construction only field that matters (read side: dts_hd_has_ma_asset()).
   has_core assumed 1 (MA=14, HD=6): matches fmp4/track.c's own "dtsh" fourcc default */
static size_t build_ext_desc_dts_hd(unsigned char *out, unsigned construction) {
  size_t n = put_desc_hdr(out, 0x7F, 1 + 7);
  out[n++] = 0x0E;
  out[n++] = 0x80; /* substream_index_flags=10000 (substream 0 present), reserved=000 */
  out[n++] = 0x05; /* substream_length=5 (bytes after this field) */
  out[n++] = 0x00; /* num_assets=0 (1 asset), channel_count=0 */
  out[n++] = 0x00; /* lfe=0, sampling_frequency=0, sample_resolution=0, reserved=00 */
  out[n++] = (unsigned char)(construction << 3); /* asset_construction(5)+vbr(0)+post_encode(0)+ctf(0) */
  out[n++] = 0x00; /* lcf(0)+bit_rate[12:6] */
  out[n++] = 0x00; /* bit_rate[5:0]+reserved */
  return n;
}

static size_t build_es_descriptor(codec_t codec, unsigned char *out, size_t cap) {
  switch (codec) {
    case CODEC_AV1: return cap >= 6 ? build_registration_desc(out, "AV01") : 0;
    case CODEC_OPUS: return cap >= 6 ? build_registration_desc(out, "Opus") : 0;
    case CODEC_DTS: return cap >= 6 ? build_registration_desc(out, "DTS2") : 0;
    case CODEC_AC4: return cap >= 3 ? build_ext_desc_ac4(out) : 0;
    case CODEC_DTS_HD: return cap >= 9 ? build_ext_desc_dts_hd(out, 6) : 0;
    case CODEC_DTS_HD_MA: return cap >= 9 ? build_ext_desc_dts_hd(out, 14) : 0;
    default: return 0;
  }
}

size_t esbuild_build_pmt(unsigned version, unsigned program_number, const esbuild_es_t *es, unsigned n_es, unsigned char *out, size_t cap) {
  size_t n = 0;
  unsigned pcr_pid;
  unsigned i;

  if (!n_es || n_es > ESBUILD_MAX_ES || cap < 32) return 0;
  pcr_pid = es[0].pid;
  for (i = 0; i < n_es; i++)
    if (is_video_codec(es[i].codec)) {
      pcr_pid = es[i].pid;
      break;
    }

  out[n++] = 0x02;
  n += 2;
  psi_put16(out + n, program_number);
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = 0x00;
  out[n++] = 0x00;
  psi_put16(out + n, 0xE000 | (pcr_pid & 0x1FFF));
  n += 2;
  psi_put16(out + n, 0xF000);
  n += 2;

  for (i = 0; i < n_es; i++) {
    size_t es_info_pos;
    unsigned esinfo;
    size_t dlen;
    if (n + 5 > cap) return 0;
    out[n++] = (unsigned char)stream_type_for(es[i].codec);
    psi_put16(out + n, 0xE000 | (es[i].pid & 0x1FFF));
    n += 2;
    es_info_pos = n;
    n += 2;
    dlen = build_es_descriptor(es[i].codec, out + n, cap - n);
    n += dlen;
    esinfo = (unsigned)dlen;
    out[es_info_pos] = (unsigned char)(0xF0 | ((esinfo >> 8) & 0x0F));
    out[es_info_pos + 1] = (unsigned char)esinfo;
  }
  return psi_finish_section(out, n, cap, 0xB0);
}
