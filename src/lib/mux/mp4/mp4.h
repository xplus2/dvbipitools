/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_MP4_H
#define DVBIPITOOLS_LIB_MUX_MP4_H

#include "lib/demux/psi/psi.h"

typedef struct {
  int audio_all;         /* mux every audio track, not just audio_track */
  unsigned audio_track;  /* 1-based audio track index, used unless audio_all */
  int subs_srt;          /* mux a teletext page as a tx3g subtitle track */
  long sub_lead_ms;      /* subtitle cues shifted earlier by this much */
} mp4_opts_t;

typedef struct mp4mux mp4_t;

/* video_ok: mp4 vs m4a. pmt_pids/n_pids: 0/NULL = single auto-lock psi_t (unchanged) */
mp4_t *mp4_new(int fd, const mp4_opts_t *opts, int video_ok, unsigned long long *bytes, const unsigned *pmt_pids, int n_pids);
void mp4_feed(mp4_t *m, const unsigned char *pkt); /* one 188-byte packet */
void mp4_close(mp4_t *m);
int  mp4_error(const mp4_t *m);

/* stream model */
const psi_t *mp4_psi(const mp4_t *m);

#endif
