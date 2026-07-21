/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "../log.h"
#include "tls.h"

/* built without OpenSSL: https:// always fails cleanly, caller still owns fd */
tls_t *tls_connect(int fd, const char *host, int insecure) {
  (void)fd;
  (void)insecure;
  log_line("tls: this build has no TLS support, cannot connect to %s over https", host);
  return NULL;
}

ssize_t tls_read(tls_t *t, void *buf, size_t cap) {
  (void)t;
  (void)buf;
  (void)cap;
  return -1;
}

ssize_t tls_write(tls_t *t, const void *buf, size_t len) {
  (void)t;
  (void)buf;
  (void)len;
  return -1;
}

void tls_close(tls_t *t) { (void)t; }
