/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <unistd.h>

#include "lib/cas/cas_dial.h"
#include "lib/helper/log.h"
#include "priv.h"

static int emmg_stopping(const emmg_server_t *s) {
  return cas_dial_stopping(&s->stop);
}

static int tcp_dial(const emmg_server_t *s, const char *host, unsigned port) {
  return cas_tcp_dial(&s->stop, host, port, EMMG_POLL_INTERVAL_MS, EMMG_CONNECT_TIMEOUT_MS, "emmg");
}

void *dial_main(void *arg) {
  emmg_server_t *s = arg;
  unsigned backoff_ms = EMMG_RECONNECT_BACKOFF_MIN_MS;
  while (!emmg_stopping(s)) {
    int fd = tcp_dial(s, s->dial_host, s->dial_port);
    if (fd < 0) {
      cas_interruptible_backoff(&s->stop, backoff_ms, EMMG_POLL_INTERVAL_MS);
      if (backoff_ms < EMMG_RECONNECT_BACKOFF_MAX_MS) backoff_ms *= 2;
      continue;
    }
    backoff_ms = EMMG_RECONNECT_BACKOFF_MIN_MS;
    atomic_store_explicit(&s->dial_connected, 1, memory_order_relaxed);
    emmg_run_session(s, fd, 0);
    atomic_store_explicit(&s->dial_connected, 0, memory_order_relaxed);
    close(fd);
  }
  return NULL;
}
