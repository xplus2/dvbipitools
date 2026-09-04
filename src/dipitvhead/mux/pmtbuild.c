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
    case CODEC_VVC:      return 0x33;
    case CODEC_MP2A:     return 0x03;
    case CODEC_AAC:      return 0x0F;
    case CODEC_AAC_LATM: return 0x11;
    case CODEC_AC3:      return 0x81;
    case CODEC_EAC3:     return 0x87;
    case CODEC_OPUS:     return 0x06;
    case CODEC_NONE:     return 0;
  }
  return 0;
}

static int supported(const psi_es_t *e, unsigned strip_mask) {
  if (e->cls == PID_VIDEO || e->cls == PID_AUDIO)
    return e->codec != CODEC_NONE;
  if (e->cls == PID_TELETEXT || e->cls == PID_SUBTITLE)
    return 1;
  if (e->cls == PID_DATA)
    return !(strip_mask & TVSTRIP_DATA);
  return 0;
}

void out_program_pids(unsigned idx, out_program_pids_t *out) {
  unsigned block_base = OUT_PROGRAM_BLOCK_BASE + idx * OUT_MAX_ES;
  out->pmt_pid = OUT_PID_PMT_BASE + idx;
  out->video_pid = block_base;
  out->es_pid_base = block_base + 1;
  out->ait_pid = block_base + OUT_PROGRAM_ES_CAP;
}

int pmtbuild_map_es(const psi_es_t *in_es, int in_count, unsigned strip_mask, unsigned src_pcr_pid, unsigned video_pid, unsigned es_pid_base, out_es_t *out_es, int cap, unsigned *pcr_pid, int *dropped) {
  int n = 0;
  unsigned next_pid = es_pid_base;

  *dropped = 0;

  /* video first, fixed pid, so PCR (usually the video pid) lands somewhere predictable */
  for (int i = 0; i < in_count && n < cap; i++) {
    if (in_es[i].cls != PID_VIDEO || !supported(&in_es[i], strip_mask))
      continue;
    out_es[n].in_pid = in_es[i].pid;
    out_es[n].out_pid = video_pid;
    out_es[n].stream_type = out_stream_type(in_es[i].codec);
    out_es[n].src = &in_es[i];
    out_es[n].is_ca = 0;
    n++;
    break; /* one video track */
  }
  for (int i = 0; i < in_count; i++) {
    if (in_es[i].cls == PID_VIDEO || !supported(&in_es[i], strip_mask))
      continue;
    if (n >= cap) {
      (*dropped)++;
      continue;
    }
    out_es[n].in_pid = in_es[i].pid;
    out_es[n].out_pid = next_pid++;
    if (in_es[i].cls == PID_TELETEXT || in_es[i].cls == PID_SUBTITLE)
      out_es[n].stream_type = 0x06;
    else if (in_es[i].cls == PID_DATA)
      out_es[n].stream_type = in_es[i].stream_type; /* opaque passthrough: keep source's own */
    else
      out_es[n].stream_type = out_stream_type(in_es[i].codec);
    out_es[n].src = &in_es[i];
    out_es[n].is_ca = 0;
    n++;
  }

  if (n > 0) {
    *pcr_pid = out_es[0].out_pid;
    for (int i = 0; i < n; i++)
      if (out_es[i].in_pid == src_pcr_pid) {
        *pcr_pid = out_es[i].out_pid;
        break;
      }
  }
  return n;
}

void pmtbuild_add_ca_passthrough(unsigned ecm_pid, unsigned ecm_ca_system_id, unsigned emm_pid, unsigned emm_ca_system_id, unsigned es_pid_base, unsigned video_pid, out_es_t *out_es, int *n, int cap, int *dropped) {
  int non_video = 0;
  unsigned next_pid;

  for (int i = 0; i < *n; i++)
    if (out_es[i].out_pid != video_pid)
      non_video++;
  next_pid = es_pid_base + (unsigned)non_video;

  if (ecm_pid) {
    if (*n >= cap) {
      (*dropped)++;
    } else {
      out_es[*n].in_pid = ecm_pid;
      out_es[*n].out_pid = next_pid++;
      out_es[*n].stream_type = 0;
      out_es[*n].src = NULL;
      out_es[*n].is_ca = 1;
      out_es[*n].ca_system_id = ecm_ca_system_id;
      (*n)++;
    }
  }
  if (emm_pid) {
    if (*n >= cap) {
      (*dropped)++;
    } else {
      out_es[*n].in_pid = emm_pid;
      out_es[*n].out_pid = next_pid++;
      out_es[*n].stream_type = 0;
      out_es[*n].src = NULL;
      out_es[*n].is_ca = 2;
      out_es[*n].ca_system_id = emm_ca_system_id;
      (*n)++;
    }
  }
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

/* copies source ES descriptor loop verbatim, minus CA_descriptor (tag 0x09): CA_PID would
   point at stale ECM pid once remapped. returns new n, or (size_t)-1 on overflow */
static size_t put_data_descriptors(unsigned char *out, size_t n, size_t cap, const psi_es_t *src) {
  size_t i = 0;
  while (i + 2 <= src->desc_len) {
    size_t l = src->desc[i + 1];
    if (i + 2 + l > src->desc_len)
      break;
    if (src->desc[i] != 0x09) {
      if (n + 2 + l > cap)
        return (size_t)-1;
      memcpy(out + n, src->desc + i, 2 + l);
      n += 2 + l;
    }
    i += 2 + l;
  }
  return n;
}

/* appends AC-3/EAC3/Opus registration + ISO 639 language descriptors for an audio ES.
   returns new n, or (size_t)-1 if it would overflow cap */
static size_t put_audio_descriptors(unsigned char *out, size_t n, size_t cap, const out_es_t *e) {
  if (e->src->codec == CODEC_AC3) {
    if (n + 6 > cap)
      return (size_t)-1;
    n += put_registration(out + n, "AC-3");
  } else if (e->src->codec == CODEC_EAC3) {
    if (n + 6 > cap)
      return (size_t)-1;
    n += put_registration(out + n, "EAC3");
  } else if (e->src->codec == CODEC_OPUS) {
    if (n + 6 > cap)
      return (size_t)-1;
    n += put_registration(out + n, "Opus");
  }
  if (e->src->cls == PID_AUDIO && e->src->lang[0]) {
    if (n + 6 > cap)
      return (size_t)-1;
    n += put_iso639(out + n, e->src->lang);
  }
  return n;
}

size_t pmtbuild_pmt(unsigned version, unsigned program_number, unsigned pcr_pid, const unsigned char *prog_desc, size_t prog_desc_len, const out_es_t *es, int es_count, const unsigned char *extra, size_t extra_len, unsigned char *out, size_t cap) {
  size_t n = 0;

  if (cap < 20 + prog_desc_len)
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
  psi_put16(out + n, (unsigned)(0xF000 | prog_desc_len));
  n += 2;
  if (prog_desc_len) {
    memcpy(out + n, prog_desc, prog_desc_len);
    n += prog_desc_len;
  }

  for (int i = 0; i < es_count; i++) {
    const out_es_t *e = &es[i];
    if (e->is_ca) /* ECM/EMM passthrough: carried as a pid, not a PMT stream entry */
      continue;
    if (n + 5 > cap)
      return 0;
    out[n++] = (unsigned char)e->stream_type;
    psi_put16(out + n, 0xE000 | (e->out_pid & 0x1FFF));
    n += 2;
    size_t es_info_pos = n;
    n += 2;

    if (e->src->cls == PID_TELETEXT) {
      if (n + 7 > cap)
        return 0;
      n += put_teletext(out + n, e->src);
    } else if (e->src->cls == PID_SUBTITLE) {
      if (n + 10 > cap)
        return 0;
      n += put_subtitling(out + n, e->src);
    } else if (e->src->cls == PID_DATA) {
      n = put_data_descriptors(out, n, cap, e->src);
      if (n == (size_t)-1)
        return 0;
    } else {
      n = put_audio_descriptors(out, n, cap, e);
      if (n == (size_t)-1)
        return 0;
    }

    unsigned esinfo = (unsigned)(n - (es_info_pos + 2));
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
