/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_ESCODEC_AUBUILD_H
#define DVBIPITOOLS_LIB_DEMUX_ESCODEC_AUBUILD_H

#include <stddef.h>

#include "escodec.h"

/* Annex B NAL unit types */
#define H264_NAL_SEI 6
#define H264_NAL_IDR 5
#define H264_NAL_SPS 7
#define H264_NAL_PPS 8
#define H264_NAL_AUD 9
#define H264_NAL_FILLER 12
#define H264_NAL_LCEVC_NON_IDR 25
#define H264_NAL_LCEVC_IDR 27

#define HEVC_NAL_IRAP_FIRST 16 /* BLA_W_LP */
#define HEVC_NAL_IRAP_LAST 21  /* CRA_NUT */
#define HEVC_NAL_VPS 32
#define HEVC_NAL_SPS 33
#define HEVC_NAL_PPS 34
#define HEVC_NAL_AUD 35
#define HEVC_NAL_FILLER 38
#define HEVC_NAL_SEI_PREFIX 39
#define HEVC_NAL_SEI_SUFFIX 40
#define HEVC_NAL_LCEVC_NON_IDR 60
#define HEVC_NAL_LCEVC_IDR 61

#define VVC_NAL_IRAP_FIRST 7 /* IDR_W_RADL */
#define VVC_NAL_IRAP_LAST 9  /* CRA_NUT, excl. GDR_NUT(10) */
#define VVC_NAL_VPS 14
#define VVC_NAL_SPS 15
#define VVC_NAL_PPS 16
#define VVC_NAL_AUD 20
#define VVC_NAL_FILLER 25 /* FD_NUT */
#define VVC_NAL_SEI_PREFIX 23
#define VVC_NAL_SEI_SUFFIX 24
#define VVC_NAL_LCEVC 31

/* NULL: inline LCEVC untouched, else strip it */
typedef struct {
  unsigned char **rb;
  size_t *rbcap;
  unsigned char **esc;
  size_t *esccap;
} lcevc_strip_t;

/* doubles *remcap as needed. 0 ok, -1 realloc failed */
int esc_rem_append(unsigned char **rem, size_t *remlen, size_t *remcap, const unsigned char *d, size_t n);

/* prefixes nal with 4-byte BE length before appending. 0 ok, -1 realloc failed */
int esc_vbuf_add(unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap, const unsigned char *nal, size_t n);

void esc_ps_store(unsigned char *dst, size_t *dlen, const unsigned char *s, size_t n);

/* param sets to es, AUD/filler dropped, rest to *vbuf via esc_vbuf_add(). sets *key on IDR/IRAP.
   strip: NULL keeps inline LCEVC as-is, else drops it (SEI-wrapped or dedicated NAL type) */
void esc_handle_h264_nal(esc_track_t *es, unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap, unsigned type, const unsigned char *p, size_t n, int *key, const lcevc_strip_t *strip);
void esc_handle_hevc_nal(esc_track_t *es, unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap, unsigned type, const unsigned char *p, size_t n, int *key, const lcevc_strip_t *strip);
void esc_handle_vvc_nal(esc_track_t *es, unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap, unsigned type, const unsigned char *p, size_t n, int *key, const lcevc_strip_t *strip);

void esc_split_nals(esc_track_t *es, unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap, const unsigned char *d, size_t len, int *key, const lcevc_strip_t *strip);

/* 0 unchanged. 1: outlen 0 drop NAL else result in *esc */
int esc_strip_lcevc_sei(const unsigned char *nal, size_t n, unsigned hdrlen, unsigned char **rb, size_t *rbcap, unsigned char **esc, size_t *esccap, size_t *outlen);

#endif
