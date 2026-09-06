/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_TLS_SERVER_H
#define DVBIPITOOLS_LIB_NET_TLS_SERVER_H

#include "tls.h"

typedef struct tls_server_ctx tls_server_ctx_t;

tls_server_ctx_t *tls_server_ctx_new(const char *cert_path, const char *key_path);
int tls_server_ctx_reload(tls_server_ctx_t *sc);
void tls_server_ctx_free(tls_server_ctx_t *sc);

tls_t *tls_server_accept_start(tls_server_ctx_t *sc, int fd);
tls_handshake_status_t tls_server_handshake_step(tls_t *t);

#endif
