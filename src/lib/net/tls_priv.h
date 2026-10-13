/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_TLS_PRIV_H
#define DVBIPITOOLS_LIB_NET_TLS_PRIV_H

#include <openssl/ssl.h>

#include "tls.h"

/* shared by tls.c (client) and tls_server.c (server). ctx: always borrowed, never owned here,
   tls_close() never frees it */
struct tls {
  SSL_CTX *ctx;
  SSL *ssl;
  int fd;
  int want_read; /* last 0 return from tls_read/tls_write was blocked on socket readability */
};

void tls_log_ssl_error(const char *what);

#endif
