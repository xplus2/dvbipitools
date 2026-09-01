/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_HTTP2_H
#define DIPIXY_HTTP2_H

#ifdef HAVE_HTTP2

#include "../reactor/conn.h"

/* attach h2 session to reactor conn: fd already TLS-handshaked, ALPN "h2",
   stays in epoll. sets c->state = CONN_H2. 0 ok, -1 OOM/init error (caller closes fd) */
int h2_conn_attach(conn_t *c);

/* EPOLLIN handler for CONN_H2 fd: reads, feeds nghttp2, dispatches ready requests, flushes TX. may close conn on error/GOAWAY */
void h2_handle_readable(int epfd, conn_t *c);

/* EPOLLOUT handler for CONN_H2 fd: drains pending output, generates + sends new nghttp2 frames */
void h2_handle_writable(int epfd, conn_t *c);

/* tear down h2 conn: free nghttp2 session, conn_t. removes fd from epfd, closes TLS */
void h2_conn_close(int epfd, conn_t *c);

/* resumes any H2 streams parked on LL-HLS blocking reload, call once per worker loop tick */
void h2_llhls_flush_waiters(void);

/* resumes any H2 streams cold-parked on index.m3u8/manifest.mpd before the first segment existed, call once per worker loop tick */
void h2_hls_cold_flush_waiters(void);

#endif /* HAVE_HTTP2 */

#endif /* DIPIXY_HTTP2_H */
