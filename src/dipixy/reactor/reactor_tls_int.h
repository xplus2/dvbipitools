/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_REACTOR_TLS_INT_H
#define DIPIXY_REACTOR_TLS_INT_H

#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <stdatomic.h>

#include "conn.h"
#include "reactor_tls.h"

typedef struct tls_gc_node {
  SSL *ssl;
  int fd;
  long long ts_ms;
  _Atomic(struct tls_gc_node *) next;
} tls_gc_node_t;

/* MPSC: any thread pushes (CAS), only tls_gc_sweep/tls_cleanup pop */
typedef struct {
  _Atomic(tls_gc_node_t *) head;
  _Atomic int count; /* approximate, caps queue growth if sweep ever stalls */
} tls_gc_shard_t;

/* reactor_tls.c: swapped wholesale by reload_tls(), never mutated in place. live SSL_CTX can be mid handshake (other thread) */
extern _Atomic(SSL_CTX *) g_ssl_ctx;
extern int g_tls_fd_max;

SSL_CTX *build_ssl_ctx(const char *cert_path, const char *key_path);
#ifdef HAVE_HTTP2
int alpn_select_cb(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen, void *arg);
#endif

/* reactor_tls_conn.c. calloc by tls_init(fd_table_cap entries) */
extern SSL **fd_ssl;
extern tls_gc_shard_t *tls_gc_shards;
extern int tls_gc_nshards;

/* reactor_tls_cert.c. subject CN in buf, 0 ok, -1 absent */
int tls_name_cn(const X509_NAME *name, char *buf, size_t bufsz);

#endif
