/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_RAWAUDIO_H
#define DVBIPITOOLS_LIB_DEMUX_RAWAUDIO_H

#include <stddef.h>

typedef void (*rawaudio_emit_cb)(void *ctx, const unsigned char *data, size_t len);
typedef int (*rawaudio_pid_excluded_cb)(const void *ctx, unsigned pid);

typedef struct rawaudio_demux rawaudio_demux_t;

/* pmt_pid 0: auto, like spts/hls. lock lowest (non excluded) audio_index PES, forward raw payload via emit(). NULL on OOM */
rawaudio_demux_t *rawaudio_demux_new(unsigned pmt_pid, rawaudio_pid_excluded_cb excluded, const void *excluded_ctx, rawaudio_emit_cb emit, void *ctx);

void rawaudio_demux_free(rawaudio_demux_t *d);

void rawaudio_demux_feed(rawaudio_demux_t *d, const unsigned char *pkt); /* 188 B */

#endif
