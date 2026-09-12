/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* cut capture_ctx_t's raw TS into hls_push_segment()-ready chunks at video keyframes */

#ifndef DIPIXY_HLS_SEGMENT_H
#define DIPIXY_HLS_SEGMENT_H

#include "../ts/capture/capture.h"
#include "../ts/lcevcselect.h"
#include "../ts/pidfilter.h"
#include "../segstore.h"

void hls_seg_init(int max_channels);

/* take ownership of ctx capture_open() ref: existing segmenter for (ctx, filter, pmt_pid) gets+drops, new one keeps it.
   caller never calls ref's capture_close(). pmt_pid: 0 = auto (first PMT that resolves), else forces one program's PMT PID.
   1 ok, ctx still valid for caller's own use. 0 failed (OOM or registry full), ctx may be pre-freed, caller must not touch it */
int hls_seg_touch(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, double seg_target, int max_segs, seg_container_t container, double part_target);

/* capture pump thread: routes one TS packet to every segmenter open for ctx */
void hls_seg_feed_all(capture_ctx_t *ctx, const unsigned char *pkt);

/* capture pump thread, roughly once a second: closes segmenters idle past 6x their own seg_target */
void hls_seg_sweep_idle(void);

#endif
