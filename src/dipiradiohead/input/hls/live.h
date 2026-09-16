/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_INPUT_HLS_LIVE_H
#define DIPIRADIOHEAD_INPUT_HLS_LIVE_H

#include <stddef.h>
#include <sys/types.h>

#include "lib/net/httpclient/httpclient.h"

typedef struct hls_live hls_live_t;

/* playlist_url: media playlist's URL (post redir). user_agent copied, need not persist. NULL=OOM */
hls_live_t *hls_live_new(const http_url_t *playlist_url, const char *user_agent, int insecure, unsigned idx, const char *label);

void hls_live_free(hls_live_t *h);

/* -1: idle between polls */
int hls_live_poll_fd(const hls_live_t *h);
short hls_live_poll_events(const hls_live_t *h);

/* >0 read, 0 transient (poll/tick again), -1 hard err */
ssize_t hls_live_read(hls_live_t *h, unsigned char *buf, size_t cap, net_err_reason_t *reason_out);

/* 1: hls_live_read() has bytes ready without waiting on poll_fd */
int hls_live_has_buffered(const hls_live_t *h);

#endif
