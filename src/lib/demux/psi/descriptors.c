/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "priv.h"

/* find descriptor by tag; returns data */
const unsigned char *find_desc(const unsigned char *d, size_t len, unsigned tag, size_t *dlen) {
  size_t i = 0;
  while (i + 2 <= len) {
    unsigned t = d[i], l = d[i + 1];
    if (i + 2 + l > len)
      break;
    if (t == tag) {
      *dlen = l;
      return d + i + 2;
    }
    i += 2 + l;
  }
  return NULL;
}

/* DVB text: skip charset prefix, controls -> space. not ISO 6937 */
void copy_name(char *dst, size_t dstsz, const unsigned char *src, size_t len) {
  size_t i = 0, o = 0;
  if (len && src[0] < 0x20) {
    if (src[0] == 0x10 && len >= 3)
      i = 3;
    else if (src[0] == 0x1F && len >= 2)
      i = 2;
    else
      i = 1;
  }
  for (; i < len && o + 1 < dstsz; i++)
    dst[o++] = (src[i] < 0x20) ? ' ' : (char)src[i];
  dst[o] = '\0';
}

void add_ecm(psi_t *c, unsigned pid) {
  if (pid == 0 || pid == 0x1FFF)
    return;
  for (int k = 0; k < c->ecm_count; k++)
    if (c->ecm[k] == pid)
      return;
  if (c->ecm_count < PSI_MAX_ES)
    c->ecm[c->ecm_count++] = pid;
}

/* parses a teletext descriptor (tag 0x56): 5-byte entries, prefers subtitle types (2/5) */
static void parse_teletext_desc(psi_es_t *e, const unsigned char *ld, size_t l) {
  e->cls = PID_TELETEXT;
  for (size_t x = 0; x + 5 <= l; x += 5) {
    int ty = ld[x + 3] >> 3;
    unsigned mag = ld[x + 3] & 0x07;
    unsigned pg = ld[x + 4];
    unsigned page = (mag ? mag : 8) * 100 + ((pg >> 4) & 0x0F) * 10 + (pg & 0x0F);
    if (!e->ttx_page || ty == 2 || ty == 5) {
      e->ttx_page = page;
      e->ttx_type = ty;
      memcpy(e->ttx_lang, ld + x, 3);
      e->ttx_lang[3] = '\0';
    }
    if (ty == 2 || ty == 5)
      break;
  }
}

/* parses a DVB subtitling descriptor (tag 0x59) */
static void parse_subtitle_desc(psi_es_t *e, const unsigned char *ld, size_t l) {
  e->cls = PID_SUBTITLE;
  if (l < 8)
    return;
  e->sub_type = ld[3];
  e->sub_composition_page = ((unsigned)ld[4] << 8) | ld[5];
  e->sub_ancillary_page = ((unsigned)ld[6] << 8) | ld[7];
  if (!e->lang[0]) {
    memcpy(e->lang, ld, 3);
    e->lang[3] = '\0';
  }
}

void classify(psi_es_t *e, const unsigned char *desc, size_t dlen) {
  size_t l;
  const unsigned char *ld;

  e->codec = CODEC_NONE;
  e->cls = PID_DATA;
  e->lang[0] = '\0';
  switch (e->stream_type) {
    case 0x01:
    case 0x02:
      e->cls = PID_VIDEO;
      e->codec = CODEC_MPEG2V;
      break;
    case 0x1B:
      e->cls = PID_VIDEO;
      e->codec = CODEC_H264;
      break;
    case 0x24:
      e->cls = PID_VIDEO;
      e->codec = CODEC_HEVC;
      break;
    case 0x03:
    case 0x04:
      e->cls = PID_AUDIO;
      e->codec = CODEC_MP2A;
      break;
    case 0x0F:
      e->cls = PID_AUDIO;
      e->codec = CODEC_AAC;
      break;
    case 0x11:
      e->cls = PID_AUDIO;
      e->codec = CODEC_AAC_LATM;
      break;
    case 0x81:
      e->cls = PID_AUDIO;
      e->codec = CODEC_AC3;
      break;
    case 0x87:
      e->cls = PID_AUDIO;
      e->codec = CODEC_EAC3;
      break;
    case 0x06:
      if ((ld = find_desc(desc, dlen, 0x56, &l)) != NULL) {
        parse_teletext_desc(e, ld, l);
      } else if ((ld = find_desc(desc, dlen, 0x59, &l)) != NULL) {
        parse_subtitle_desc(e, ld, l);
      } else if (find_desc(desc, dlen, 0x6A, &l)) {
        e->cls = PID_AUDIO;
        e->codec = CODEC_AC3;
      } else if (find_desc(desc, dlen, 0x7A, &l)) {
        e->cls = PID_AUDIO;
        e->codec = CODEC_EAC3;
      }
      break;
    case 0x05:
      if (find_desc(desc, dlen, 0x6F, &l))
        e->cls = PID_AIT;
      break;
    default:
      break;
  }

  ld = find_desc(desc, dlen, 0x0A, &l);
  if (ld && l >= 3) {
    e->lang[0] = (char)ld[0];
    e->lang[1] = (char)ld[1];
    e->lang[2] = (char)ld[2];
    e->lang[3] = '\0';
  }
}

/* service_descriptor (0x48): provider then service name, DVB-text
   length-prefixed. dst untouched if absent/malformed. */
void decode_service_desc(const unsigned char *d, size_t dll, char *provider_dst, char *service_dst) {
  size_t l, pnl, snl;
  const unsigned char *sd = find_desc(d, dll, 0x48, &l);
  if (!sd || l < 2)
    return;
  pnl = sd[1];
  if (2 + pnl > l)
    return;
  copy_name(provider_dst, PSI_NAME, sd + 2, pnl);
  if (2 + pnl >= l)
    return;
  snl = sd[2 + pnl];
  if (3 + pnl + snl <= l)
    copy_name(service_dst, PSI_NAME, sd + 3 + pnl, snl);
}
