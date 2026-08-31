/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_REACTOR_TLS_H
#define DIPIXY_REACTOR_TLS_H

#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
  char cn[256];
  char valid_from[64];
  char valid_to[64];
  char aliases[16][256]; /* SAN DNS names */
  int alias_count;
} tls_cert_detail_t;

/* both defined in net.c. t_reactor_fd >= 0 and matching fd: close recorded in t_close_deferred, not performed, reactor flushes first.
   default -1: every close immediate, legacy path unaffected */
extern _Thread_local int t_reactor_fd;
extern _Thread_local int t_close_deferred;
/* after EAGAIN: 1 if OpenSSL wanted writability, 0 if readability. reactor arms correct epoll event during handshake/renegotiation.
   always 0 on non-TLS path */
extern _Thread_local int t_tls_want_write;

extern _Thread_local int t_reactor_tid;

/* call before tls_init(): 0 = ALPN skip h2. default enabled */
void tls_set_http2_enabled(int enabled);
int tls_init(const char *cert_path, const char *key_path, int fd_table_cap); /* init SSL_CTX once before spawning threads, -1: caller aborts */
void tls_gc_init(int nshards); /* one GC queue per worker thread, before spawning them */
void tls_cleanup(void);
int tls_create_listen_sock(int port, const char *host);   /* per-thread, IPv4 */
int tls_create_listen_sock6(int port, const char *host6); /* per-thread, IPv6 */
int tls_is_running(void); /* non-zero when SSL_CTX is active */
int tls_accept(int fd);
int tls_handshake(int fd); /* 1=done, 0=needs I/O (EAGAIN), -1=error */
void tls_close_fd(int fd);
void tls_gc_sweep(
    void); /* free deferred-close SSL objects older than g_tls_socket_gc_ms */
ssize_t tls_net_send(int fd, const void *buf, size_t len);
ssize_t tls_net_recv(int fd, void *buf, size_t len);
/* MSG_ZEROCOPY send, falls back to plain send() on EINVAL/ENOBUFS. used_zc: 1 if zerocopy hit */
ssize_t tls_net_send_zc(int fd, const void *buf, size_t len, int *used_zc);
/* drains MSG_ERRQUEUE, hi_out = highest confirmed zc id. 1 if any found */
int tls_zc_drain(int fd, int *hi_out);
int tls_is_tls(int fd);
int tls_client_cert_verified(int fd);
/* CN of verified client cert into buf, empty string if none */
void tls_get_client_cert_cn(int fd, char *buf, size_t bufsz);
int reload_tls(void); /* rereads cert/key from tls_init()'s paths. 0 ok, -1 fail (old ctx kept running) */
void tls_cert_info(char *buf, size_t sz, const char *path, int from_file);

int tls_has_pending(int fd);

/* non-zero when ALPN negotiated h2 on fd (requires HAVE_HTTP2) */
int tls_alpn_is_h2(int fd);

/* structured cert details from in-memory context (from_file=0) or PEM file at path (from_file=1). 1 ok, 0 fail */
int tls_cert_detail(const char *path, int from_file, tls_cert_detail_t *out);

#endif /* DIPIXY_REACTOR_TLS_H */

