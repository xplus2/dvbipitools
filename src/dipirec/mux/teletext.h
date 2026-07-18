/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_MUX_TELETEXT_H
#define DIPIREC_MUX_TELETEXT_H

#include <stddef.h>
#include <stdint.h>

#define TTX_TEXT_MAX 1024

/* finished subtitle; UTF-8, LF joined */
typedef struct {
    int64_t start_ms, end_ms;
    char text[TTX_TEXT_MAX];
} ttx_cue_t;

typedef void (*ttx_cb)(void *ctx, const ttx_cue_t *cue);
typedef struct ttx ttx_t;

/* decode subs from teletext page */
/* lead_ms shifts cues earlier; txt usually lags behind speech */
ttx_t *ttx_new(unsigned page, const char *lang, long lead_ms, ttx_cb cb, void *ctx);
void ttx_free(ttx_t *t);

/* one teletext PES payload (EN 300 472 du) */
void ttx_pes(ttx_t *t, int has_pts, uint64_t pts, const unsigned char *d, size_t len);

/* close cue still on screen */
void ttx_flush(ttx_t *t);

#endif
