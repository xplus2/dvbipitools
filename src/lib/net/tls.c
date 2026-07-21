/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include "../log.h"
#include "tls.h"

struct tls {
  SSL_CTX *ctx;
  SSL *ssl;
  int fd;
};

static void log_ssl_error(const char *what) {
  unsigned long e = ERR_get_error();
  char buf[256];
  if (e) {
    ERR_error_string_n(e, buf, sizeof buf);
    log_line("%s: %s", what, buf);
  } else {
    log_line("%s: unknown TLS error", what);
  }
}

tls_t *tls_connect(int fd, const char *host, int insecure) {
  tls_t *t = calloc(1, sizeof *t);
  if (!t)
    return NULL;
  t->fd = fd;
  t->ctx = SSL_CTX_new(TLS_client_method());
  if (!t->ctx) {
    log_ssl_error("SSL_CTX_new");
    goto fail;
  }
  if (insecure) {
    SSL_CTX_set_verify(t->ctx, SSL_VERIFY_NONE, NULL);
  } else {
    SSL_CTX_set_verify(t->ctx, SSL_VERIFY_PEER, NULL);
    if (SSL_CTX_set_default_verify_paths(t->ctx) != 1) {
      log_ssl_error("SSL_CTX_set_default_verify_paths");
      goto fail;
    }
  }
  t->ssl = SSL_new(t->ctx);
  if (!t->ssl) {
    log_ssl_error("SSL_new");
    goto fail;
  }
  if (SSL_set_tlsext_host_name(t->ssl, host) != 1) {
    log_ssl_error("SSL SNI setup");
    goto fail;
  }
  if (!insecure && SSL_set1_host(t->ssl, host) != 1) {
    log_ssl_error("SSL hostname verification setup");
    goto fail;
  }
  if (SSL_set_fd(t->ssl, fd) != 1) {
    log_ssl_error("SSL_set_fd");
    goto fail;
  }
  if (SSL_connect(t->ssl) != 1) {
    log_line("tls handshake with %s failed", host);
    log_ssl_error("SSL_connect");
    goto fail;
  }
  return t;

fail:
  if (t->ssl)
    SSL_free(t->ssl);
  if (t->ctx)
    SSL_CTX_free(t->ctx);
  free(t);
  return NULL;
}

ssize_t tls_read(tls_t *t, void *buf, size_t cap) {
  int n, err;

  if (cap > INT_MAX)
    cap = INT_MAX;
  n = SSL_read(t->ssl, buf, (int)cap);
  if (n > 0)
    return n;
  err = SSL_get_error(t->ssl, n);
  if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
    return 0;
  if (err != SSL_ERROR_ZERO_RETURN)
    log_ssl_error("SSL_read");
  return -1;
}

ssize_t tls_write(tls_t *t, const void *buf, size_t len) {
  int n, err;
  if (len > INT_MAX)
    len = INT_MAX;
  n = SSL_write(t->ssl, buf, (int)len);
  if (n > 0)
    return n;
  err = SSL_get_error(t->ssl, n);
  if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
    return 0;
  log_ssl_error("SSL_write");
  return -1;
}

void tls_close(tls_t *t) {
  if (!t)
    return;
  if (t->ssl) {
    SSL_shutdown(t->ssl);
    SSL_free(t->ssl);
  }
  if (t->ctx)
    SSL_CTX_free(t->ctx);
  if (t->fd >= 0)
    close(t->fd);
  free(t);
}
