/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_FLV_H
#define DVBIPITOOLS_LIB_MUX_FLV_H

#include <stddef.h>
#include <stdint.h>

#include "lib/demux/psi/psi.h"

typedef enum { FLV_TAG_AUDIO = 8, FLV_TAG_VIDEO = 9, FLV_TAG_SCRIPT = 18 } flv_tag_type_t;

/* tag body only, TagType/DataSize/Timestamp/StreamID stripped. data valid
   for call duration only. */
typedef void (*flv_tag_cb)(void *ctx, flv_tag_type_t type, uint32_t timestamp_ms, const unsigned char *data, size_t len);

typedef struct {
  unsigned audio_track; /* 1-based audio_index, 0 = first available */
} flv_opts_t;

typedef struct flv flv_t;

/* RTMP model: 1 video + 1 audio track max. pmt_pid 0 = auto-lock psi_t.
   cb: onMetaData once, seqhdr per track, then per-AU tags. cb_ctx opaque. */
flv_t *flv_new(const flv_opts_t *opts, unsigned pmt_pid, flv_tag_cb cb, void *cb_ctx, unsigned long long *bytes);
void flv_feed(flv_t *f, const unsigned char *pkt); /* one 188-byte packet */
void flv_close(flv_t *f);                          /* EOS: flush pending PES */
int flv_error(const flv_t *f);

const psi_t *flv_psi(const flv_t *f);

#endif
