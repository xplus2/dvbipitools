/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_RAWAUDIO_H
#define DIPIXY_RAWAUDIO_H

#include <stddef.h>

#include "pidfilter.h"

typedef void (*rawaudio_emit_cb)(void *ctx, const unsigned char *data, size_t len);

typedef struct rawaudio_demux rawaudio_demux_t;

/* pmt_pid 0: auto, like spts/hls. lock lowest (non excluded) audio_index PES, forward raw payload via emit(). NULL on OOM */
rawaudio_demux_t *rawaudio_demux_new(unsigned pmt_pid, const pid_filter_t *filter, rawaudio_emit_cb emit, void *ctx);

void rawaudio_demux_free(rawaudio_demux_t *d);

void rawaudio_demux_feed(rawaudio_demux_t *d, const unsigned char *pkt); /* 188 B */

#endif
