/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_FLV_PRIV_H
#define DVBIPITOOLS_LIB_MUX_FLV_PRIV_H

#include <stdint.h>

#include "flv.h"
#include "lib/demux/escodec/escodec.h"
#include "lib/demux/pes.h"
#include "lib/demux/psi/psi.h"

#define FLV_REM_MAX 65536

typedef struct {
  unsigned pid;
  pid_class_t cls;
  int hdr_parsed;
  int64_t ts_ms;
  pts_unwrap_t pts;
  unsigned char *rem; /* audio: partial frame carry-over */
  size_t remlen, remcap;
  unsigned char *vbuf; /* video: length-prefixed AU (AVCC/HVCC framed NALUs) */
  size_t vbuflen, vbufcap;
  int got_key;      /* first keyframe seen */
  int seqhdr_sent;  /* avcC/hvcC/ASC sequence-start tag already emitted */
  esc_track_t es;
} flv_track_t;

struct flv {
  const flv_opts_t *opts;
  unsigned long long *bytes;
  psi_t *psi;
  pes_t *pes;
  flv_track_t vtrk, atrk;
  int have_v, have_a;
  int setup, started, err, flushing;
  int64_t t0;
  flv_tag_cb cb;
  void *cb_ctx;
};

/* feed.c */
void flv_setup(flv_t *f);
void flv_on_pes(void *ctx, unsigned pid, int has_pts, uint64_t pts, const unsigned char *data, size_t len);
void flv_all_ready(flv_t *f);

/* write.c */
void flv_send_tag(flv_t *f, flv_tag_type_t type, uint32_t ts, const unsigned char *hdr, size_t hn, const unsigned char *payload, size_t pn);
void flv_emit_metadata(flv_t *f);
void flv_emit_video(flv_t *f, flv_track_t *t, const unsigned char *d, size_t n, int key);
void flv_emit_audio(flv_t *f, flv_track_t *t, const unsigned char *d, size_t n);

#endif
