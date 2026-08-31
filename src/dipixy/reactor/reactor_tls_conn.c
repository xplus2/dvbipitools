/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "reactor_tls_int.h"

#include <errno.h>
#include <openssl/x509.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

SSL **fd_ssl;
tls_gc_shard_t *tls_gc_shards = NULL;
int tls_gc_nshards = 0;

/* no immediate SSL_free()! other thread may still be in SSL_read/write.
   GC frees entries older than g_tls_socket_gc_ms */
#define TLS_GC_MAX 4096
#define TLS_GC_MS 2000

void tls_gc_init(int nshards) {
  if (nshards < 1) nshards = 1;
  tls_gc_shards = calloc((size_t)nshards, sizeof *tls_gc_shards);
  if (!tls_gc_shards) return;
  tls_gc_nshards = nshards;
}

/* out-of-range tid: shard 0. still correct, just more contended */
static tls_gc_shard_t *tls_gc_shard_for_tid(void) {
  int tid = t_reactor_tid;
  if (!tls_gc_shards) return NULL;
  if (tid < 0 || tid >= tls_gc_nshards) tid = 0;
  return &tls_gc_shards[tid];
}

static long long tls_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int tls_is_tls(int fd) {
  return (fd >= 0 && fd < g_tls_fd_max && __atomic_load_n(&fd_ssl[fd], __ATOMIC_ACQUIRE) != NULL);
}

int tls_client_cert_verified(int fd) {
  if (fd < 0 || fd >= g_tls_fd_max) return 0;
  SSL *ssl = __atomic_load_n(&fd_ssl[fd], __ATOMIC_ACQUIRE);
  if (!ssl) return 0;
  if (SSL_get_verify_result(ssl) != X509_V_OK) return 0;
  X509 *cert = SSL_get_peer_certificate(ssl);
  if (!cert) return 0;
  X509_free(cert);
  return 1;
}

void tls_get_client_cert_cn(int fd, char *buf, size_t bufsz) {
  if (!buf || !bufsz) return;
  buf[0] = '\0';
  if (fd < 0 || fd >= g_tls_fd_max) return;
  SSL *ssl = __atomic_load_n(&fd_ssl[fd], __ATOMIC_ACQUIRE);
  if (!ssl) return;
  X509 *cert = SSL_get_peer_certificate(ssl);
  if (!cert) return;
  X509_NAME *subj = X509_get_subject_name(cert);
  if (subj) X509_NAME_get_text_by_NID(subj, NID_commonName, buf, (int)bufsz);
  X509_free(cert);
}

/* non-blocking, handshake completes lazily on first SSL_read/write */
int tls_accept(int fd) {
  if (!g_ssl_ctx || fd < 0 || fd >= g_tls_fd_max) return -1;
  SSL *ssl = SSL_new(g_ssl_ctx);
  if (!ssl) return -1;
  SSL_set_fd(ssl, fd);
  SSL_set_accept_state(ssl);
  __atomic_store_n(&fd_ssl[fd], ssl, __ATOMIC_RELEASE);
  return 0;
}

/* explicit handshake: ALPN (h2 vs http/1.1) known before payload. 1=done, 0=need more, -1=error.
   block sock: block until done (bound by SO_RCVTIMEO) */
int tls_handshake(int fd) {
  if (fd < 0 || fd >= g_tls_fd_max) return -1;
  SSL *ssl = __atomic_load_n(&fd_ssl[fd], __ATOMIC_ACQUIRE);
  if (!ssl) return -1;
  int r = SSL_do_handshake(ssl);
  if (r == 1) return 1;
  int err = SSL_get_error(ssl, r);
  if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
    t_tls_want_write = (err == SSL_ERROR_WANT_WRITE);
    errno = EAGAIN;
    return 0;
  }
  return -1;
}

void tls_close_fd(int fd) {
  /* reactor: defers close -> buffered output flushes first (see tls.h). t_reactor_fd = -1 on blocking path: no-op there */
  if (t_reactor_fd >= 0 && fd == t_reactor_fd) {
    t_close_deferred = 1;
    return;
  }
  if (fd < 0 || fd >= g_tls_fd_max) {
    if (fd >= 0) close(fd);
    return;
  }

  SSL *ssl = __atomic_exchange_n(&fd_ssl[fd], (SSL *)NULL, __ATOMIC_ACQ_REL);
  if (!ssl) {
    close(fd);
    return;
  }

  /* best-effort close_notify. guarded: mid-handshake SSL_shutdown() yields "shutdown while in init", noise not signal. 2-byte alert goes to sock buf */
  if (SSL_is_init_finished(ssl)) SSL_shutdown(ssl);

  /* interrupts blocking SSL_read/write in another thread: shutdown() delivers immediate error.
     fd stays allocated (not returned to OS), number can't be reused b4 tls_gc_sweep() closes after GC */
  shutdown(fd, SHUT_RDWR);

  /* Schedule deferred SSL_free + close after g_tls_socket_gc_ms. */
  {
    tls_gc_shard_t *shard = tls_gc_shard_for_tid();
    tls_gc_node_t *node, *old_head;
    if (!shard || atomic_fetch_add_explicit(&shard->count, 1, memory_order_relaxed) >= TLS_GC_MAX) {
      if (shard) atomic_fetch_sub_explicit(&shard->count, 1, memory_order_relaxed);
      /* no shard, or GC queue full: free immediately, shouldn't happen in practice */
      if (SSL_is_init_finished(ssl))
        SSL_shutdown(ssl);
      SSL_free(ssl);
      close(fd);
      return;
    }
    node = malloc(sizeof *node);
    if (!node) {
      atomic_fetch_sub_explicit(&shard->count, 1, memory_order_relaxed);
      if (SSL_is_init_finished(ssl)) SSL_shutdown(ssl);
      SSL_free(ssl);
      close(fd);
      return;
    }
    node->ssl = ssl;
    node->fd = fd;
    node->ts_ms = tls_now_ms();
    old_head = atomic_load_explicit(&shard->head, memory_order_relaxed);
    for (;;) {
      atomic_store_explicit(&node->next, old_head, memory_order_relaxed);
      if (atomic_compare_exchange_weak_explicit(&shard->head, &old_head, node, memory_order_release, memory_order_relaxed)) break;
    }
  }
}

void tls_gc_sweep(void) {
  long long now = tls_now_ms();
  for (int s = 0; s < tls_gc_nshards; s++) {
    tls_gc_shard_t *shard = &tls_gc_shards[s];
    tls_gc_node_t *chain = atomic_exchange_explicit(&shard->head, NULL, memory_order_acquire);
    tls_gc_node_t *keep_head = NULL, *keep_tail = NULL;
    int reclaimed = 0;
    while (chain) {
      tls_gc_node_t *next = atomic_load_explicit(&chain->next, memory_order_relaxed);
      if (now - chain->ts_ms >= TLS_GC_MS) {
        /* close_notify already attempted in tls_close_fd. retry only for fully-handshaked sessions, no "shutdown while in init" noise */
        if (SSL_is_init_finished(chain->ssl)) SSL_shutdown(chain->ssl);
        SSL_free(chain->ssl);
        close(chain->fd);
        free(chain);
        reclaimed++;
      } else {
        atomic_store_explicit(&chain->next, keep_head, memory_order_relaxed);
        keep_head = chain;
        if (!keep_tail) keep_tail = chain;
      }
      chain = next;
    }
    if (reclaimed) atomic_fetch_sub_explicit(&shard->count, reclaimed, memory_order_relaxed);
    if (keep_head) {
      tls_gc_node_t *old_head = atomic_load_explicit(&shard->head, memory_order_relaxed);
      for (;;) {
        atomic_store_explicit(&keep_tail->next, old_head, memory_order_relaxed);
        if (atomic_compare_exchange_weak_explicit(&shard->head, &old_head, keep_head, memory_order_release, memory_order_relaxed))
          break;
      }
    }
  }
}

int tls_has_pending(int fd) {
  if (fd < 0 || fd >= g_tls_fd_max) return 0;
  SSL *ssl = __atomic_load_n(&fd_ssl[fd], __ATOMIC_ACQUIRE);
  return ssl ? SSL_pending(ssl) > 0 : 0;
}

int tls_alpn_is_h2(int fd) {
#ifdef HAVE_HTTP2
  if (fd < 0 || fd >= g_tls_fd_max) return 0;
  SSL *ssl = __atomic_load_n(&fd_ssl[fd], __ATOMIC_ACQUIRE);
  if (!ssl) return 0;
  const unsigned char *proto = NULL;
  unsigned int protolen = 0;
  SSL_get0_alpn_selected(ssl, &proto, &protolen);
  return (protolen == 2 && proto[0] == 'h' && proto[1] == '2');
#else
  (void)fd;
  return 0;
#endif
}
