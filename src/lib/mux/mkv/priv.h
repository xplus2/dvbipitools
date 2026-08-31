/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_MKV_PRIV_H
#define DVBIPITOOLS_LIB_MUX_MKV_PRIV_H

#include <stdint.h>

#include "../ebml.h"
#include "../teletext.h"
#include "lib/demux/escodec/escodec.h"
#include "lib/demux/pes.h"
#include "lib/demux/psi/psi.h"
#include "mkv.h"

#define MKV_MAX_TRACKS 8
#define MKV_MAX_PROGRAMS 8
#define MKV_PEND_MAX 1024
#define MKV_PEND_BYTES (8u * 1024 * 1024)
#define MKV_REM_MAX 65536
#define CLUSTER_MS 30000

typedef struct {
  unsigned pid;
  int num;
  pid_class_t cls;
  char lang[4];
  char codecid[24];
  unsigned width, height;    /* v */
  int hdr_parsed;
  int64_t ts_ms;
  pts_unwrap_t pts;
  int psi_idx; /* m->psi[] index. TrackName resolved late in write_head(), sdt may lag setup() */
  unsigned char *rem; /* audio: partial frame carry-over */
  size_t remlen, remcap;
  unsigned char *vbuf; /* video: length-prefixed AU */
  size_t vbuflen, vbufcap;
  int got_key; /* first keyframe seen */
  ttx_t *ttx;  /* damn teletext */
  esc_track_t es; /* avcC/hvcC/ASC, param sets, LATM cache */
} track_t;

typedef struct {
  int num, key;
  int64_t ts, dur;
  unsigned char *data;
  size_t len;
} pend_t;

struct mkv {
  int fd;
  const mkv_opts_t *opts;
  int video_ok;
  unsigned long long *bytes;
  psi_t *psi[MKV_MAX_PROGRAMS];
  int npsi;
  pes_t *pes;
  track_t trk[MKV_MAX_TRACKS];
  int ntrk;
  int setup, started, err;
  int flushing; /* EOS: last PES partial */
  int ready_seen;
  int64_t ready_ms; /* when tracks first had headers */
  int64_t t0;
  pend_t pend[MKV_PEND_MAX];
  int npend;
  size_t pend_bytes;
  unsigned char *pend_arena; /* MKV_PEND_BYTES, bump-allocated by pend_add, reset with pend_bytes */
  ebuf_t cl;
  int64_t cl_base;
  int cl_open;
};

/* video.c: codec_id_for only, rest -> lib/demux/escodec */
const char *codec_id_for(codec_t codec, const esc_frame_t *f);

/* write.c */
void wfd(mkv_t *m, const void *p, size_t n);
track_t *find_track(mkv_t *m, unsigned pid);
void cluster_flush(mkv_t *m);
void put_block(mkv_t *m, int num, int64_t rel, const unsigned char *d, size_t n, int key, int64_t dur);
void start(mkv_t *m);
void pend_add(mkv_t *m, int num, int64_t ts, const unsigned char *d, size_t n, int key, int64_t dur);
void emit(mkv_t *m, track_t *t, const unsigned char *d, size_t n, int key);

/* feed.c */
void all_ready(mkv_t *m);
void on_pes(void *ctx, unsigned pid, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *data, size_t len);
void setup(mkv_t *m);

#endif
