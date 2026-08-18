/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_ESCODEC_AUBUILD_H
#define DVBIPITOOLS_LIB_DEMUX_ESCODEC_AUBUILD_H

#include <stddef.h>

#include "escodec.h"

/* Annex B NAL unit types */
#define H264_NAL_IDR 5
#define H264_NAL_SPS 7
#define H264_NAL_PPS 8
#define H264_NAL_AUD 9
#define H264_NAL_FILLER 12

#define HEVC_NAL_IRAP_FIRST 16 /* BLA_W_LP */
#define HEVC_NAL_IRAP_LAST 21  /* CRA_NUT */
#define HEVC_NAL_VPS 32
#define HEVC_NAL_SPS 33
#define HEVC_NAL_PPS 34
#define HEVC_NAL_AUD 35
#define HEVC_NAL_FILLER 38

/* doubles *remcap as needed. 0 ok, -1 realloc failed */
int esc_rem_append(unsigned char **rem, size_t *remlen, size_t *remcap, const unsigned char *d, size_t n);

/* prefixes nal with 4-byte BE length before appending. 0 ok, -1 realloc failed */
int esc_vbuf_add(unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap, const unsigned char *nal, size_t n);

void esc_ps_store(unsigned char *dst, size_t *dlen, const unsigned char *s, size_t n);

/* param sets to es, AUD/filler dropped, rest to *vbuf via esc_vbuf_add(). sets *key on IDR/IRAP */
void esc_handle_h264_nal(esc_track_t *es, unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap, unsigned type, const unsigned char *p, size_t n, int *key);
void esc_handle_hevc_nal(esc_track_t *es, unsigned char **vbuf, size_t *vbuflen, size_t *vbufcap, unsigned type, const unsigned char *p, size_t n, int *key);

#endif
