/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/demux/bitreader.h"
#include "lib/helper/ioutil.h"

#include "aubuild.h"

int esc_rem_append(unsigned char **rem, size_t *remlen, size_t *remcap, const unsigned char *d, size_t n) {
  if (growbuf_reserve((void **)rem, remcap, 1, *remlen + n, 8192)) return -1;
  memcpy(*rem + *remlen, d, n);
  *remlen += n;
  return 0;
}

int esc_vbuf_add(unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap, const unsigned char *nal, size_t n) {
  size_t need = *vbuflen + 4 + n;
  if (growbuf_reserve((void **)vbuf, vbufcap, 1, need, 65536)) return -1;
  (*vbuf)[(*vbuflen)++] = (unsigned char)(n >> 24);
  (*vbuf)[(*vbuflen)++] = (unsigned char)(n >> 16);
  (*vbuf)[(*vbuflen)++] = (unsigned char)(n >> 8);
  (*vbuf)[(*vbuflen)++] = (unsigned char)n;
  memcpy(*vbuf + *vbuflen, nal, n);
  *vbuflen += n;
  return 0;
}

void esc_ps_store(unsigned char *dst, size_t *dlen, const unsigned char *s, size_t n) {
  if (n && n <= ESCODEC_PS_MAX) {
    memcpy(dst, s, n);
    *dlen = n;
  }
}

/* 1: handled (kept/dropped). 0: not sei or off */
static int emit_sei_maybe_stripped(esc_track_t *es, unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap,
                                   const unsigned char *p, size_t n, unsigned hdrlen, const lcevc_strip_t *strip) {
  size_t outlen;
  if (!strip) return 0;
  if (!esc_strip_lcevc_sei(p, n, hdrlen, strip->rb, strip->rbcap, strip->esc, strip->esccap, &outlen)) return 0;
  if (outlen && esc_vbuf_add(vbuf, vbuflen, vbufcap, *strip->esc, outlen) < 0)
    log_throttled(&es->vbuf_drop_throttle, LOG_THROTTLE_WINDOW_S, "escodec: esc_vbuf_add failed, sei nal dropped");
  return 1;
}

void esc_handle_h264_nal(esc_track_t *es, unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap, unsigned type, const unsigned char *p, size_t n, int *key, const lcevc_strip_t *strip) {
  switch (type) {
    case H264_NAL_SPS:    esc_ps_store(es->sps, &es->spslen, p, n);   break;
    case H264_NAL_PPS:    esc_ps_store(es->pps, &es->ppslen, p, n);   break;
    case H264_NAL_AUD:
    case H264_NAL_FILLER:                                             break;
    case H264_NAL_LCEVC_NON_IDR:
    case H264_NAL_LCEVC_IDR:
      if (strip) break;
      if (esc_vbuf_add(vbuf, vbuflen, vbufcap, p, n) < 0) log_throttled(&es->vbuf_drop_throttle, LOG_THROTTLE_WINDOW_S, "escodec: esc_vbuf_add failed, h264 nal dropped");
      break;
    case H264_NAL_SEI:
      if (emit_sei_maybe_stripped(es, vbuf, vbuflen, vbufcap, p, n, 1, strip)) break;
      if (esc_vbuf_add(vbuf, vbuflen, vbufcap, p, n) < 0) log_throttled(&es->vbuf_drop_throttle, LOG_THROTTLE_WINDOW_S, "escodec: esc_vbuf_add failed, h264 nal dropped");
      break;
    default:
      if (type == H264_NAL_IDR) *key = 1;
      if (esc_vbuf_add(vbuf, vbuflen, vbufcap, p, n) < 0) log_throttled(&es->vbuf_drop_throttle, LOG_THROTTLE_WINDOW_S, "escodec: esc_vbuf_add failed, h264 nal dropped");
  }
}

void esc_handle_hevc_nal(esc_track_t *es, unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap, unsigned type, const unsigned char *p, size_t n, int *key, const lcevc_strip_t *strip) {
  switch (type) {
    case HEVC_NAL_VPS:      esc_ps_store(es->vps, &es->vpslen, p, n);      break;
    case HEVC_NAL_SPS:      esc_ps_store(es->sps, &es->spslen, p, n);      break;
    case HEVC_NAL_PPS:      esc_ps_store(es->pps, &es->ppslen, p, n);      break;
    case HEVC_NAL_AUD:
    case HEVC_NAL_FILLER:                                                  break;
    case HEVC_NAL_LCEVC_NON_IDR:
    case HEVC_NAL_LCEVC_IDR:
      if (strip) break;
      if (esc_vbuf_add(vbuf, vbuflen, vbufcap, p, n) < 0) log_throttled(&es->vbuf_drop_throttle, LOG_THROTTLE_WINDOW_S, "escodec: esc_vbuf_add failed, hevc nal dropped");
      break;
    case HEVC_NAL_SEI_PREFIX:
      if (emit_sei_maybe_stripped(es, vbuf, vbuflen, vbufcap, p, n, 2, strip)) break;
      if (esc_vbuf_add(vbuf, vbuflen, vbufcap, p, n) < 0) log_throttled(&es->vbuf_drop_throttle, LOG_THROTTLE_WINDOW_S, "escodec: esc_vbuf_add failed, hevc nal dropped");
      break;
    default:
      if (type >= HEVC_NAL_IRAP_FIRST && type <= HEVC_NAL_IRAP_LAST) *key = 1;
      if (esc_vbuf_add(vbuf, vbuflen, vbufcap, p, n) < 0) log_throttled(&es->vbuf_drop_throttle, LOG_THROTTLE_WINDOW_S, "escodec: esc_vbuf_add failed, hevc nal dropped");
  }
}

int esc_strip_lcevc_sei(const unsigned char *nal, size_t n, unsigned hdrlen, unsigned char **rb, size_t *rbcap, unsigned char **esc, size_t *esccap, size_t *outlen) {
  size_t rblen;
  size_t w = 0;
  size_t esc_len;
  size_t i = 0;
  int removed = 0;
  int malformed = 0;

  if (n <= hdrlen) return 0;
  if (growbuf_reserve((void **)rb, rbcap, 1, n - hdrlen + 1, 4096)) return 0;
  rblen = rbsp_unescape(nal + hdrlen, n - hdrlen, *rb, n - hdrlen);
  while (i + 2 <= rblen) {
    size_t msg_start = i;
    size_t size = 0;
    unsigned type = 0;
    while (i < rblen && (*rb)[i] == 0xFF) {
      type += 255;
      i++;
    }
    if (i >= rblen) {
      malformed = 1;
      break;
    }
    type += (*rb)[i++];
    while (i < rblen && (*rb)[i] == 0xFF) {
      size += 255;
      i++;
    }
    if (i >= rblen) {
      malformed = 1;
      break;
    }
    size += (*rb)[i++];
    if (i + size > rblen) {
      malformed = 1;
      break;
    }
    if (type == 4 && size >= 4 && (*rb)[i] == 0xB4 && (*rb)[i + 1] == 0x00 && (*rb)[i + 2] == 0x50 && (*rb)[i + 3] == 0x00) {
      removed = 1;
    } else {
      size_t msg_len = (i + size) - msg_start;
      if (w != msg_start) memmove(*rb + w, *rb + msg_start, msg_len);
      w += msg_len;
    }
    i += size;
  }
  if (malformed || !removed) return 0;
  if (!w) {
    *outlen = 0;
    return 1;
  }
  (*rb)[w++] = 0x80; /* leftover B was rbsp_trailing_bits, regen */
  if (growbuf_reserve((void **)esc, esccap, 1, hdrlen + w + w / 2 + 8, 4096)) return 0;
  memcpy(*esc, nal, hdrlen);
  esc_len = rbsp_escape(*rb, w, *esc + hdrlen, *esccap - hdrlen);
  if (!esc_len) return 0;
  *outlen = hdrlen + esc_len;
  return 1;
}

void esc_handle_vvc_nal(esc_track_t *es, unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap, unsigned type, const unsigned char *p, size_t n, int *key, const lcevc_strip_t *strip) {
  switch (type) {
    case VVC_NAL_VPS:      esc_ps_store(es->vps, &es->vpslen, p, n);      break;
    case VVC_NAL_SPS:      esc_ps_store(es->sps, &es->spslen, p, n);      break;
    case VVC_NAL_PPS:      esc_ps_store(es->pps, &es->ppslen, p, n);      break;
    case VVC_NAL_AUD:
    case VVC_NAL_FILLER:                                                  break;
    case VVC_NAL_LCEVC:
      if (strip) break;
      if (esc_vbuf_add(vbuf, vbuflen, vbufcap, p, n) < 0) log_throttled(&es->vbuf_drop_throttle, LOG_THROTTLE_WINDOW_S, "escodec: esc_vbuf_add failed, vvc nal dropped");
      break;
    case VVC_NAL_SEI_PREFIX:
      if (emit_sei_maybe_stripped(es, vbuf, vbuflen, vbufcap, p, n, 2, strip)) break;
      if (esc_vbuf_add(vbuf, vbuflen, vbufcap, p, n) < 0) log_throttled(&es->vbuf_drop_throttle, LOG_THROTTLE_WINDOW_S, "escodec: esc_vbuf_add failed, vvc nal dropped");
      break;
    default:
      if (type >= VVC_NAL_IRAP_FIRST && type <= VVC_NAL_IRAP_LAST) *key = 1;
      if (esc_vbuf_add(vbuf, vbuflen, vbufcap, p, n) < 0) log_throttled(&es->vbuf_drop_throttle, LOG_THROTTLE_WINDOW_S, "escodec: esc_vbuf_add failed, vvc nal dropped");
  }
}

void esc_split_nals(esc_track_t *es, unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap, const unsigned char *d, size_t len, int *key, const lcevc_strip_t *strip) {
  size_t p;
  size_t scl = 0;
  p = find_startcode(d, len, 0, &scl);
  while (p < len) {
    size_t ns = p + scl;
    size_t scl2 = 0;
    size_t q = find_startcode(d, len, ns, &scl2);
    size_t n = q - ns;
    unsigned type;
    if (n) {
      if (es->codec == CODEC_H264) {
        type = d[ns] & 0x1F;
        esc_handle_h264_nal(es, vbuf, vbuflen, vbufcap, type, d + ns, n, key, strip);
      } else if (es->codec == CODEC_HEVC) {
        type = (d[ns] >> 1) & 0x3F;
        esc_handle_hevc_nal(es, vbuf, vbuflen, vbufcap, type, d + ns, n, key, strip);
      } else {
        type = n > 1 ? (d[ns + 1] >> 3) & 0x1F : 0;
        esc_handle_vvc_nal(es, vbuf, vbuflen, vbufcap, type, d + ns, n, key, strip);
      }
    }
    p = q;
    scl = scl2;
  }
}
