/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_TLS_H
#define DVBIPITOOLS_LIB_NET_TLS_H

#include <stddef.h>
#include <sys/types.h>

typedef struct tls tls_t;

/* TLS client handshake on connected fd; SNI=host, verified against system CA unless insecure. on success owns fd, else caller does */
tls_t *tls_connect(int fd, const char *host, int insecure);

/* like recv(): >0 bytes, 0 = transient (would block), -1 = error/closed */
ssize_t tls_read(tls_t *t, void *buf, size_t cap);

/* like send(): >0 bytes, 0 = transient (would block), -1 = error */
ssize_t tls_write(tls_t *t, const void *buf, size_t len);

void tls_close(tls_t *t);

#endif
