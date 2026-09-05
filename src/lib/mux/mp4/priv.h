/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_MP4_PRIV_H
#define DVBIPITOOLS_LIB_MUX_MP4_PRIV_H

#include <stdint.h>

#include "../fmp4/box.h"
#include "../teletext.h"
#include "lib/demux/escodec/escodec.h"
#include "lib/demux/pes.h"
#include "lib/demux/psi/psi.h"
#include "mp4.h"

#define MP4_MAX_TRACKS 8
#define MP4_MAX_PROGRAMS 8
#define MP4_PEND_MAX 1024
#define MP4_PEND_BYTES (8u * 1024 * 1024)
#define MP4_REM_MAX 65536
#define MP4_TIMESCALE 1000 /* ms, every track, no cross-track rescale needed */

typedef struct {
  uint64_t offset;
  uint32_t size;
  uint32_t duration; /* patched in later for video, see prev_dur_slot */
  int32_t cts_offset; /* pts - dts, ms. 0 for audio/subs */
  int keyframe;
} mp4_samp_t;

typedef struct {
  unsigned pid;
  unsigned track_id;
  pid_class_t cls;
  char lang[4];
  unsigned width, height;
  int hdr_parsed;
  int psi_idx; /* m->psi[] index */
  int64_t ts_ms;      /* video: dts. audio/subs: pts (no reordering, dts==pts) */
  int64_t pts_ms;     /* video only, for cts_offset */
  pts_unwrap_t pts_uw, dts_uw;
  unsigned char *rem; /* audio: partial frame carry-over */
  size_t remlen, remcap;
  unsigned char *vbuf; /* video: length-prefixed AU */
  size_t vbuflen, vbufcap;
  int got_key;
  ttx_t *ttx;
  esc_track_t es;
  unsigned char ac3_bsid, ac3_bsmod, ac3_acmod, ac3_lfeon; /* AC3/EAC3 dac3/dec3, captured from 1st frame */
  unsigned ac3_bitrate_code;

  mp4_samp_t *samp;
  int nsamp, samp_cap;
  uint32_t *prev_dur_slot;
  int64_t prev_dts_ms;
  uint32_t last_dur;
} track_t;

typedef struct {
  int trk;
  int64_t ts_ms;
  int32_t cts_offset;
  int key;
  uint32_t dur;
  unsigned char *data;
  size_t len;
} pend_t;

struct mp4mux {
  int fd;
  const mp4_opts_t *opts;
  int video_ok;
  unsigned long long *bytes;
  psi_t *psi[MP4_MAX_PROGRAMS];
  int npsi;
  pes_t *pes;
  track_t trk[MP4_MAX_TRACKS];
  int ntrk;
  int setup, started, err;
  int flushing;
  int ready_seen;
  int64_t ready_ms;
  int64_t t0;
  pend_t pend[MP4_PEND_MAX];
  int npend;
  size_t pend_bytes;
  unsigned char *pend_arena;
  uint64_t mdat_hdr_pos; /* file offset of mdat's size+largesize fields */
};

/* video.c: mp4-specific sample entry fourccs, rest -> lib/demux/escodec */
const char *p4_entry_fourcc_for(codec_t codec);

/* write.c */
void p4_wfd(mp4_t *m, const void *p, size_t n);
track_t *p4_find_track(mp4_t *m, unsigned pid);
void p4_start(mp4_t *m);
void p4_pend_add(mp4_t *m, int trk, int64_t ts, int32_t cts, const unsigned char *d, size_t n, int key, uint32_t dur);
void p4_write_sample(mp4_t *m, track_t *t, int32_t cts, const unsigned char *d, size_t n, int key, uint32_t dur);
void p4_route(mp4_t *m, track_t *t, int64_t ts_ms, int32_t cts, const unsigned char *d, size_t n, int key, uint32_t dur);

/* moov.c */
void p4_write_ftyp_mdat_head(mp4_t *m);
void p4_write_moov(mp4_t *m);

/* feed.c */
void p4_all_ready(mp4_t *m);
void p4_on_pes(void *ctx, unsigned pid, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *data, size_t len);
void p4_setup(mp4_t *m);

#endif
