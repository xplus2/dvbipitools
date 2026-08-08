/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIMETRICS_HTTPSERVER_H
#define DIPIMETRICS_HTTPSERVER_H

#include "store.h"

/* binds+listens (nonblocking fd, SO_REUSEADDR). -1 on failure (logged) */
int http_listen(int family, const char *addr, unsigned port);

/* accept pending connection off a nonblocking fd, handles it synchronously to completion.
   bounded read/write budget (via SO_RCVTIMEO/SO_SNDTIMEO plus a wall-clock deadline) keeps one slow or
   hostile client from wedging the collector for more than a few seconds.
   GET /metrics renders current store; anything else 404. no-op if no conn pending. */
void http_accept_and_serve(int listen_fd, const store_t *st, double now_mono, int verbose);

#endif
