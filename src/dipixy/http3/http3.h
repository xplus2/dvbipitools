/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_HTTP3_H
#define DIPIXY_HTTP3_H

#ifdef HAVE_HTTP3

#include <netinet/in.h>
#include <stddef.h>
#include <sys/socket.h>

/* set by reactor.c before its event loop starts, -1 if unbound */
extern _Thread_local int t_h3_udp4;
extern _Thread_local int t_h3_udp6;

/* per-thread UDP/QUIC listen socket, unregistered with epoll. -1 on failure */
int h3_create_udp_sock(int port, const char *host);
int h3_create_udp_sock6(int port, const char *host6);

/* call on EPOLLIN for a QUIC UDP socket */
void h3_handle_readable(int udp_fd);

/* periodic maintenance: expire idle conns, flush deferred TX */
void h3_tick(void);

/* resumes any H3 streams parked on LL-HLS blocking reload, call once per tick */
void h3_llhls_flush_waiters(void);

/* resumes cold-parked H3 manifest/playlist streams, call once per tick */
void h3_hls_cold_flush_waiters(void);

/* resumes H3 WS streams with data queued from any thread, call once per tick */
void h3_ws_flush(void);

/* ms until nearest QUIC timer, for epoll_wait sizing. -1 = no active conns */
int h3_next_timeout_ms(void);

/* releases thread's live QUIC conns, called once per worker on exit */
void h3_thread_cleanup(void);

/* global HTTP/3 state (SSL_CTX for QUIC). called once after tls_init() */
void h3_init(const char *cert_path, const char *key_path);

void h3_cleanup(void);

#endif /* HAVE_HTTP3 */

#endif /* DIPIXY_HTTP3_H */
