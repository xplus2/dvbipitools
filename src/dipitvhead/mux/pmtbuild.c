/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/mux/psi_build.h"

#include "pmtbuild.h"

/* stream_type per codec, matches reference multicasts where verified live */
static unsigned out_stream_type(codec_t c) {
  switch (c) {
    case CODEC_MPEG2V:   return 0x02;
    case CODEC_H264:     return 0x1B;
    case CODEC_HEVC:     return 0x24;
    case CODEC_MP2A:     return 0x03;
    case CODEC_AAC:      return 0x0F;
    case CODEC_AAC_LATM: return 0x11;
    case CODEC_AC3:      return 0x81;
    case CODEC_EAC3:     return 0x87;
    case CODEC_NONE:     return 0;
  }
  return 0;
}

static int supported(const psi_es_t *e) {
  if (e->cls == PID_VIDEO || e->cls == PID_AUDIO)
    return e->codec != CODEC_NONE;
  return e->cls == PID_TELETEXT || e->cls == PID_SUBTITLE;
}

int pmtbuild_map_es(const psi_es_t *in_es, int in_count, unsigned src_pcr_pid, out_es_t *out_es, int cap, unsigned *pcr_pid) {
  int i, n = 0;
  unsigned next_pid = OUT_PID_ES_BASE;

  /* video first, fixed pid, so PCR (usually the video pid) lands somewhere predictable */
  for (i = 0; i < in_count && n < cap; i++) {
    if (in_es[i].cls != PID_VIDEO || !supported(&in_es[i]))
      continue;
    out_es[n].in_pid = in_es[i].pid;
    out_es[n].out_pid = OUT_PID_VIDEO;
    out_es[n].stream_type = out_stream_type(in_es[i].codec);
    out_es[n].src = &in_es[i];
    n++;
    break; /* one video track */
  }
  for (i = 0; i < in_count && n < cap; i++) {
    if (in_es[i].cls == PID_VIDEO || !supported(&in_es[i]))
      continue;
    out_es[n].in_pid = in_es[i].pid;
    out_es[n].out_pid = next_pid++;
    out_es[n].stream_type = (in_es[i].cls == PID_TELETEXT || in_es[i].cls == PID_SUBTITLE) ? 0x06 : out_stream_type(in_es[i].codec);
    out_es[n].src = &in_es[i];
    n++;
  }

  if (n > 0) {
    *pcr_pid = out_es[0].out_pid;
    for (i = 0; i < n; i++)
      if (out_es[i].in_pid == src_pcr_pid) {
        *pcr_pid = out_es[i].out_pid;
        break;
      }
  }
  return n;
}

static size_t put_registration(unsigned char *out, const char *fourcc) {
  out[0] = 0x05;
  out[1] = 4;
  memcpy(out + 2, fourcc, 4);
  return 6;
}

static size_t put_iso639(unsigned char *out, const char *lang) {
  out[0] = 0x0A;
  out[1] = 4;
  memcpy(out + 2, lang, 3);
  out[5] = 0x00; /* audio_type: undefined */
  return 6;
}

static size_t put_teletext(unsigned char *out, const psi_es_t *e) {
  unsigned mag = (e->ttx_page / 100 == 8) ? 0 : (e->ttx_page / 100);
  unsigned page = e->ttx_page % 100;
  out[0] = 0x56;
  out[1] = 5;
  memcpy(out + 2, e->ttx_lang[0] ? e->ttx_lang : "und", 3);
  out[5] = (unsigned char)((e->ttx_type << 3) | (mag & 0x07));
  out[6] = (unsigned char)(((page / 10) << 4) | (page % 10));
  return 7;
}

static size_t put_subtitling(unsigned char *out, const psi_es_t *e) {
  out[0] = 0x59;
  out[1] = 8;
  memcpy(out + 2, e->lang[0] ? e->lang : "und", 3);
  out[5] = (unsigned char)e->sub_type;
  psi_put16(out + 6, e->sub_composition_page);
  psi_put16(out + 8, e->sub_ancillary_page);
  return 10;
}

size_t pmtbuild_pmt(unsigned version, unsigned program_number, unsigned pcr_pid, const out_es_t *es, int es_count, const unsigned char *extra, size_t extra_len, unsigned char *out, size_t cap) {
  size_t n = 0, es_info_pos;
  int i;
  unsigned esinfo;

  if (cap < 20)
    return 0;
  out[n++] = 0x02;
  n += 2;
  psi_put16(out + n, program_number);
  n += 2;
  out[n++] = (unsigned char)(0xC0 | ((version & 0x1F) << 1) | 0x01);
  out[n++] = 0x00;
  out[n++] = 0x00;
  psi_put16(out + n, 0xE000 | (pcr_pid & 0x1FFF));
  n += 2;
  psi_put16(out + n, 0xF000); /* program_info_length = 0 */
  n += 2;

  for (i = 0; i < es_count; i++) {
    const out_es_t *e = &es[i];
    if (n + 5 > cap)
      return 0;
    out[n++] = (unsigned char)e->stream_type;
    psi_put16(out + n, 0xE000 | (e->out_pid & 0x1FFF));
    n += 2;
    es_info_pos = n;
    n += 2;

    if (e->src->cls == PID_TELETEXT) {
      if (n + 7 > cap)
        return 0;
      n += put_teletext(out + n, e->src);
    } else if (e->src->cls == PID_SUBTITLE) {
      if (n + 10 > cap)
        return 0;
      n += put_subtitling(out + n, e->src);
    } else {
      if (e->src->codec == CODEC_AC3) {
        if (n + 6 > cap)
          return 0;
        n += put_registration(out + n, "AC-3");
      } else if (e->src->codec == CODEC_EAC3) {
        if (n + 6 > cap)
          return 0;
        n += put_registration(out + n, "EAC3");
      }
      if (e->src->cls == PID_AUDIO && e->src->lang[0]) {
        if (n + 6 > cap)
          return 0;
        n += put_iso639(out + n, e->src->lang);
      }
    }

    esinfo = (unsigned)(n - (es_info_pos + 2));
    out[es_info_pos] = (unsigned char)(0xF0 | ((esinfo >> 8) & 0x0F));
    out[es_info_pos + 1] = (unsigned char)esinfo;
  }
  if (extra_len) {
    if (n + extra_len > cap)
      return 0;
    memcpy(out + n, extra, extra_len);
    n += extra_len;
  }
  return psi_finish_section(out, n, cap, 0xB0);
}
