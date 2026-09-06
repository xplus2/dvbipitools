/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "../helper/log.h"
#include "tls_server.h"

tls_server_ctx_t *tls_server_ctx_new(const char *cert_path, const char *key_path) {
  (void)cert_path;
  (void)key_path;
  log_line("tls: this build has no TLS support, cannot serve https");
  return NULL;
}

int tls_server_ctx_reload(tls_server_ctx_t *sc) {
  (void)sc;
  return -1;
}

void tls_server_ctx_free(tls_server_ctx_t *sc) { (void)sc; }

tls_t *tls_server_accept_start(tls_server_ctx_t *sc, int fd) {
  (void)sc;
  (void)fd;
  return NULL;
}

tls_handshake_status_t tls_server_handshake_step(tls_t *t) {
  (void)t;
  return TLS_HANDSHAKE_ERROR;
}
