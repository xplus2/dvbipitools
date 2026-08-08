/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_TLS_H
#define DVBIPITOOLS_LIB_NET_TLS_H

#include <stddef.h>
#include <sys/types.h>

typedef struct tls tls_t;

/* TLS client handshake on connected fd; SNI=host, verified against system CA unless insecure. on success owns fd, else caller does.
   fd must be a blocking socket - SSL_connect() here runs to completion or failure, no WANT_READ/WRITE handling. */
tls_t *tls_connect(int fd, const char *host, int insecure);

typedef enum { TLS_HANDSHAKE_DONE, TLS_HANDSHAKE_WANT_READ, TLS_HANDSHAKE_WANT_WRITE, TLS_HANDSHAKE_ERROR } tls_handshake_status_t;

/* async pair, for non-blocking fd (caller's own poll() loop). same setup/verification as tls_connect(),
   but returns after one SSL_connect() attempt instead of blocking to completion. on WANT_READ/WANT_WRITE,
   poll fd for matching direction, then call tls_handshake_step() again.
   on DONE/ERROR the handle is usable/dead the same as if tls_connect() had returned it/NULL;
   ERROR already logged, caller just tls_close()s.
   NULL return here is only a setup-time failure, not a handshake WANT_* - fd ownership on that path stays with the caller. */
tls_t *tls_connect_start(int fd, const char *host, int insecure);
tls_handshake_status_t tls_handshake_step(tls_t *t);

/* like recv(): >0 bytes, 0 = transient (would block), -1 = error/closed */
ssize_t tls_read(tls_t *t, void *buf, size_t cap);

/* like send(): >0 bytes, 0 = transient (would block), -1 = error */
ssize_t tls_write(tls_t *t, const void *buf, size_t len);

void tls_close(tls_t *t);

#endif
