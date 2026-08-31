/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/helper/ioutil.h"

#include "aubuild.h"

int esc_rem_append(unsigned char **rem, size_t *remlen, size_t *remcap, const unsigned char *d, size_t n) {
  if (growbuf_reserve((void **)rem, remcap, 1, *remlen + n, 8192))
    return -1;
  memcpy(*rem + *remlen, d, n);
  *remlen += n;
  return 0;
}

int esc_vbuf_add(unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap, const unsigned char *nal, size_t n) {
  size_t need = *vbuflen + 4 + n;

  if (growbuf_reserve((void **)vbuf, vbufcap, 1, need, 65536))
    return -1;
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

void esc_handle_h264_nal(esc_track_t *es, unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap,
                          unsigned type, const unsigned char *p, size_t n, int *key) {
  switch (type) {
    case H264_NAL_SPS:
      esc_ps_store(es->sps, &es->spslen, p, n);
      break;
    case H264_NAL_PPS:
      esc_ps_store(es->pps, &es->ppslen, p, n);
      break;
    case H264_NAL_AUD:
    case H264_NAL_FILLER:
      break;
    default:
      if (type == H264_NAL_IDR)
        *key = 1;
      esc_vbuf_add(vbuf, vbuflen, vbufcap, p, n);
  }
}

void esc_handle_hevc_nal(esc_track_t *es, unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap, unsigned type, const unsigned char *p, size_t n, int *key) {
  switch (type) {
    case HEVC_NAL_VPS:
      esc_ps_store(es->vps, &es->vpslen, p, n);
      break;
    case HEVC_NAL_SPS:
      esc_ps_store(es->sps, &es->spslen, p, n);
      break;
    case HEVC_NAL_PPS:
      esc_ps_store(es->pps, &es->ppslen, p, n);
      break;
    case HEVC_NAL_AUD:
    case HEVC_NAL_FILLER:
      break;
    default:
      if (type >= HEVC_NAL_IRAP_FIRST && type <= HEVC_NAL_IRAP_LAST)
        *key = 1;
      esc_vbuf_add(vbuf, vbuflen, vbufcap, p, n);
  }
}
