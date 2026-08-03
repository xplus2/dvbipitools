/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_MKV_H
#define DVBIPITOOLS_LIB_MUX_MKV_H

#include "lib/demux/psi.h"

typedef struct {
  int audio_all;          /* mux every audio track, not just audio_track */
  unsigned audio_track;    /* 1-based audio track index, used unless audio_all */
  int subs_srt;            /* mux a teletext page as an S_TEXT/UTF8 subtitle track */
  long sub_lead_ms;         /* subtitle cues shifted earlier by this much */
  const char *app_name;    /* Segment MuxingApp/WritingApp, e.g. "dipirec 1.2.3" */
  const char *source_desc; /* SOURCE tag; NULL/empty to omit */
} mkv_opts_t;

typedef struct mkv mkv_t;

/* stream Matroska to fd. video_ok: mkv vs mka. bytes: running output size */
mkv_t *mkv_new(int fd, const mkv_opts_t *opts, int video_ok, unsigned long long *bytes);
void mkv_feed(mkv_t *m, const unsigned char *pkt);   /* one 188-byte packet */
void mkv_close(mkv_t *m);                            /* chron-o-john, free Bernard */
int  mkv_error(const mkv_t *m);

/* stream model */
const psi_t *mkv_psi(const mkv_t *m);

#endif
