/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _DEFAULT_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef HAVE_HTTP2
#include <nghttp2/nghttp2.h>
#endif

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"

#include "internal.h"
#ifdef HAVE_HTTP3
#include "../http3/http3.h"
#endif
#include "reactor_tls_int.h"
#include "../version.h"

/* swapped wholesale by reload_tls(), never mutated in place: a live SSL_CTX
   can be mid-handshake on another thread's SSL_new() call at any moment */
_Atomic(SSL_CTX *) g_ssl_ctx = NULL;
int g_tls_fd_max = 0;

static int g_http2_enabled = 1;

void tls_set_http2_enabled(int enabled) { g_http2_enabled = enabled; }

#ifdef HAVE_HTTP2
int alpn_select_cb(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen, void *arg) {
  static const unsigned char http11[] = "\x08http/1.1";
  unsigned char *sel = NULL;
  (void)ssl;
  (void)arg;
  if (g_http2_enabled && nghttp2_select_next_protocol(&sel, outlen, in, inlen) == 1) {
    *out = sel;
    return SSL_TLSEXT_ERR_OK;
  }
  if (SSL_select_next_proto(&sel, outlen, http11, sizeof http11 - 1, in, inlen) == OPENSSL_NPN_NEGOTIATED) {
    *out = sel;
    return SSL_TLSEXT_ERR_OK;
  }
  return SSL_TLSEXT_ERR_NOACK;
}
#endif

static char g_tls_cert[512];
static char g_tls_key[512];

/* fresh SSL_CTX, cert/key loaded, NULL on any failure. same options set as
   tls_init(), reused by reload_tls() so a reload gets identical ctx config */
SSL_CTX *build_ssl_ctx(const char *cert_path, const char *key_path) {
  SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
  if (!ctx) return NULL;

  SSL_CTX_set_mode(ctx, SSL_MODE_RELEASE_BUFFERS);
  SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER);

  if (SSL_CTX_set_cipher_list(ctx,
          "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
          "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:"
          "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256") != 1) {
    SSL_CTX_free(ctx);
    return NULL;
  }
#ifdef TLS1_3_VERSION
  if (SSL_CTX_set_ciphersuites(ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256") != 1) {
    SSL_CTX_free(ctx);
    return NULL;
  }
#endif
  SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION | SSL_OP_CIPHER_SERVER_PREFERENCE | SSL_OP_NO_RENEGOTIATION);
  SSL_CTX_set1_groups_list(ctx, "X25519:P-256");
  SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
  SSL_CTX_set_mode(ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

#ifdef SSL_OP_ENABLE_KTLS
  SSL_CTX_set_options(ctx, SSL_OP_ENABLE_KTLS);
#endif

  SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

#ifdef HAVE_HTTP2
  SSL_CTX_set_alpn_select_cb(ctx, alpn_select_cb, NULL);
#endif

  if (SSL_CTX_use_certificate_chain_file(ctx, cert_path) != 1 || SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) != 1) {
    SSL_CTX_free(ctx);
    return NULL;
  }
  return ctx;
}

/* 0: ok, or TLS off on bad cert. -1: fd_table_cap exceeds fd_ssl's cap, caller aborts */
int tls_init(const char *cert_path, const char *key_path, int fd_table_cap) {
  SSL_CTX *ctx;
  bufcpy(g_tls_cert, sizeof g_tls_cert, cert_path);
  bufcpy(g_tls_key, sizeof g_tls_key, key_path);

  OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
  ctx = build_ssl_ctx(cert_path, key_path);
  if (!ctx) return 0;

  if (fd_table_cap > CONN_TABLE_MAX) {
    log_code("YS000671", "tls_init: fd table cap %d exceeds max supported %d", fd_table_cap, CONN_TABLE_MAX);
    SSL_CTX_free(ctx);
    return -1;
  }
  fd_ssl = calloc((size_t)fd_table_cap, sizeof *fd_ssl);
  if (!fd_ssl) {
    log_code("YA500212", TOOL_NAME ": out of memory sizing fd_ssl table (%d entries), TLS not bound", fd_table_cap);
    SSL_CTX_free(ctx);
    return 0;
  }
  g_tls_fd_max = fd_table_cap;
  g_ssl_ctx = ctx;
  log_line_ansi("tls: context ready fd_ssl=\e[0;37m%d\e[0m", g_tls_fd_max);
  return 0;
}

int tls_is_running(void) { return (g_ssl_ctx != NULL); }

void tls_cleanup(void) {
  /* flush pending GC entries before tearing down SSL context */
  for (int s = 0; s < tls_gc_nshards; s++) {
    tls_gc_shard_t *shard = &tls_gc_shards[s];
    tls_gc_node_t *chain = atomic_exchange_explicit(&shard->head, NULL, memory_order_acquire);
    while (chain) {
      tls_gc_node_t *next = atomic_load_explicit(&chain->next, memory_order_relaxed);
      SSL_free(chain->ssl);
      close(chain->fd);
      free(chain);
      chain = next;
    }
  }
  free(tls_gc_shards);
  tls_gc_shards = NULL;
  tls_gc_nshards = 0;
  if (g_ssl_ctx) {
    SSL_CTX_free(g_ssl_ctx);
    g_ssl_ctx = NULL;
  }
  g_tls_fd_max = 0;
}

#define TLS_CTX_RETIRE_MAX 8

typedef struct {
  SSL_CTX *ctx;
  uint64_t mark[QSBR_MAX_WORKERS];
} tls_ctx_retiree_t;

static tls_ctx_retiree_t g_ctx_retiring[TLS_CTX_RETIRE_MAX];
static int g_ctx_retiring_n;
static pthread_mutex_t g_ctx_retiring_lock = PTHREAD_MUTEX_INITIALIZER;

void tls_ctx_gc_sweep(void) {
  pthread_mutex_lock(&g_ctx_retiring_lock);
  for (int i = 0; i < g_ctx_retiring_n;) {
    if (qsbr_mark_passed(reactor_qsbr(), g_ctx_retiring[i].mark)) {
      SSL_CTX_free(g_ctx_retiring[i].ctx);
      g_ctx_retiring[i] = g_ctx_retiring[--g_ctx_retiring_n];
    } else {
      i++;
    }
  }
  pthread_mutex_unlock(&g_ctx_retiring_lock);
}

/* defers SSL_CTX_free until every worker passed a quiescent point:
   a concurrent SSL_new() may still hold the pointer. swept by tls_ctx_gc_sweep() */
void tls_ctx_retire(SSL_CTX *old) {
  pthread_mutex_lock(&g_ctx_retiring_lock);
  if (g_ctx_retiring_n < TLS_CTX_RETIRE_MAX) {
    qsbr_mark(reactor_qsbr(), g_ctx_retiring[g_ctx_retiring_n].mark);
    g_ctx_retiring[g_ctx_retiring_n].ctx = old;
    g_ctx_retiring_n++;
  } else {
    log_code("YW500213", TOOL_NAME ": tls reload: retire queue full, leaking one SSL_CTX");
  }
  pthread_mutex_unlock(&g_ctx_retiring_lock);
}

/* builds fresh SSL_CTX, swaps it in. never mutates live ctx's cert store in place:
   concurrent SSL_new() calls need to see whole old/new ctx */
int reload_tls(void) {
  SSL_CTX *fresh, *old;
  if (!atomic_load_explicit(&g_ssl_ctx, memory_order_acquire)) return -1;
  fresh = build_ssl_ctx(g_tls_cert, g_tls_key);
  if (!fresh) return -1;
  old = atomic_exchange_explicit(&g_ssl_ctx, fresh, memory_order_acq_rel);
  tls_ctx_gc_sweep();
  tls_ctx_retire(old);
#ifdef HAVE_HTTP3
  if (h3_ready() && h3_reload() != 0) return -1;
#endif
  return 0;
}
