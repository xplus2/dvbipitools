/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_TLS_PRIV_H
#define DVBIPITOOLS_LIB_NET_TLS_PRIV_H

#include <openssl/ssl.h>

#include "tls.h"

/* shared by tls.c (client) and tls_server.c (server). ctx: per-connection for a client handle.
   NULL for server-accepted (owned by tls_server_ctx_t), tls_close() then skips freeing it */
struct tls {
  SSL_CTX *ctx;
  SSL *ssl;
  int fd;
};

#endif
