/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include "../helper/ioutil.h"
#include "../helper/log.h"
#include "tls_priv.h"
#include "tls_server.h"

struct tls_server_ctx {
  SSL_CTX *ctx;
  char cert_path[512];
  char key_path[512];
};

static SSL_CTX *build_ctx(const char *cert_path, const char *key_path) {
  SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
  if (!ctx) {
    tls_log_ssl_error("SSL_CTX_new");
    return NULL;
  }
  if (SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION) != 1) {
    tls_log_ssl_error("SSL_CTX_set_min_proto_version");
    SSL_CTX_free(ctx);
    return NULL;
  }
  if (SSL_CTX_use_certificate_chain_file(ctx, cert_path) != 1 || SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) != 1 ||
      SSL_CTX_check_private_key(ctx) != 1) {
    log_line("tls: failed to load cert %s / key %s", cert_path, key_path);
    tls_log_ssl_error("cert/key load");
    SSL_CTX_free(ctx);
    return NULL;
  }
  return ctx;
}

tls_server_ctx_t *tls_server_ctx_new(const char *cert_path, const char *key_path) {
  tls_server_ctx_t *sc = calloc(1, sizeof *sc);
  if (!sc) return NULL;
  sc->ctx = build_ctx(cert_path, key_path);
  if (!sc->ctx) {
    free(sc);
    return NULL;
  }
  bufcpy(sc->cert_path, sizeof sc->cert_path, cert_path);
  bufcpy(sc->key_path, sizeof sc->key_path, key_path);
  return sc;
}

int tls_server_ctx_reload(tls_server_ctx_t *sc) {
  SSL_CTX *fresh = build_ctx(sc->cert_path, sc->key_path);
  if (!fresh) return -1;
  SSL_CTX_free(sc->ctx);
  sc->ctx = fresh;
  return 0;
}

void tls_server_ctx_free(tls_server_ctx_t *sc) {
  if (!sc) return;
  SSL_CTX_free(sc->ctx);
  free(sc);
}

tls_t *tls_server_accept_start(tls_server_ctx_t *sc, int fd) {
  tls_t *t = calloc(1, sizeof *t);
  if (!t) return NULL;
  t->fd = fd;
  t->ctx = NULL;
  t->ssl = SSL_new(sc->ctx);
  if (!t->ssl) {
    tls_log_ssl_error("SSL_new");
    free(t);
    return NULL;
  }
  if (SSL_set_fd(t->ssl, fd) != 1) {
    tls_log_ssl_error("SSL_set_fd");
    SSL_free(t->ssl);
    free(t);
    return NULL;
  }
  return t;
}

tls_handshake_status_t tls_server_handshake_step(tls_t *t) {
  int r = SSL_accept(t->ssl);
  int err;
  if (r == 1) return TLS_HANDSHAKE_DONE;
  err = SSL_get_error(t->ssl, r);
  if (err == SSL_ERROR_WANT_READ) return TLS_HANDSHAKE_WANT_READ;
  if (err == SSL_ERROR_WANT_WRITE) return TLS_HANDSHAKE_WANT_WRITE;
  log_line("tls handshake failed");
  tls_log_ssl_error("SSL_accept");
  return TLS_HANDSHAKE_ERROR;
}
