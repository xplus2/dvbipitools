/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/signal.h"
#include "priv.h"

static int emmg_stopping(const emmg_server_t *s) {
  return atomic_load_explicit(&s->stop, memory_order_relaxed) || signal_stop_requested();
}

/* 1 connected, -1 refused/err, 0 timed out, stop request */
static int wait_connect(emmg_server_t *s, int fd) {
  int elapsed = 0;
  while (elapsed < EMMG_CONNECT_TIMEOUT_MS) {
    struct pollfd pfd;
    int step = EMMG_POLL_INTERVAL_MS;
    int pret;
    if (emmg_stopping(s)) return 0;
    if (step > EMMG_CONNECT_TIMEOUT_MS - elapsed) step = EMMG_CONNECT_TIMEOUT_MS - elapsed;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    pret = poll(&pfd, 1, step);
    if (pret > 0) {
      int soerr = 0;
      socklen_t sl = sizeof soerr;
      getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
      if (soerr == 0) return 1;
      errno = soerr;
      return -1;
    }
    if (pret < 0 && errno != EINTR) return -1;
    elapsed += step;
  }
  return 0;
}

static int tcp_dial(emmg_server_t *s, const char *host, unsigned port) {
  struct addrinfo hints;
  struct addrinfo *res;
  char portstr[6];
  int fd = -1;
  int e;
  int save_errno = 0;

  uint_to_str(portstr, port);
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  e = getaddrinfo(host, portstr, &hints, &res);
  if (e) {
    log_line("emmg: resolve %s: %s", host, gai_strerror(e));
    return -1;
  }
  for (const struct addrinfo *ai = res; ai; ai = ai->ai_next) {
    int flags;
    int cr;
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) continue;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      save_errno = errno;
      close(fd);
      fd = -1;
      continue;
    }
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
    if (errno != EINPROGRESS) {
      save_errno = errno;
      close(fd);
      fd = -1;
      continue;
    }
    cr = wait_connect(s, fd);
    if (cr == 1) break;
    save_errno = (cr == -1) ? errno : ETIMEDOUT;
    close(fd);
    fd = -1;
    if (emmg_stopping(s)) break;
  }
  freeaddrinfo(res);
  if (fd < 0) {
    if (!emmg_stopping(s)) log_line("emmg: connect %s:%u: %s", host, port, strerror(save_errno));
    return -1;
  }
  return fd;
}

static void interruptible_backoff(emmg_server_t *s, unsigned ms) {
  unsigned waited = 0;
  while (waited < ms && !emmg_stopping(s)) {
    unsigned step = (ms - waited < EMMG_POLL_INTERVAL_MS) ? ms - waited : EMMG_POLL_INTERVAL_MS;
    struct timespec ts = {step / 1000, (long)(step % 1000) * 1000000L};
    nanosleep(&ts, NULL);
    waited += step;
  }
}

void *dial_main(void *arg) {
  emmg_server_t *s = arg;
  unsigned backoff_ms = EMMG_RECONNECT_BACKOFF_MIN_MS;
  while (!emmg_stopping(s)) {
    int fd = tcp_dial(s, s->dial_host, s->dial_port);
    if (fd < 0) {
      interruptible_backoff(s, backoff_ms);
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
