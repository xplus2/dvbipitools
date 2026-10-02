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

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/sys/signal.h"

#include "cas_dial.h"

int cas_dial_stopping(const _Atomic int *stop) {
  return atomic_load_explicit(stop, memory_order_relaxed) || signal_stop_requested();
}

static int wait_connect(const _Atomic int *stop, int fd, unsigned poll_interval_ms, unsigned timeout_ms) {
  unsigned elapsed = 0;
  while (elapsed < timeout_ms) {
    struct pollfd pfd;
    unsigned step = poll_interval_ms;
    int pret;
    if (cas_dial_stopping(stop)) return 0;
    if (step > timeout_ms - elapsed) step = timeout_ms - elapsed;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    pret = poll(&pfd, 1, (int)step);
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

static int dial_addr(const _Atomic int *stop, const struct addrinfo *ai, unsigned poll_interval_ms, unsigned timeout_ms, int *save_errno) {
  int flags;
  int cr;
  int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
  if (fd < 0) return -1;
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    *save_errno = errno;
    close(fd);
    return -1;
  }
  if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) return fd;
  if (errno != EINPROGRESS) {
    *save_errno = errno;
    close(fd);
    return -1;
  }
  cr = wait_connect(stop, fd, poll_interval_ms, timeout_ms);
  if (cr == 1) return fd;
  *save_errno = (cr == -1) ? errno : ETIMEDOUT;
  close(fd);
  return -1;
}

int cas_tcp_dial(const _Atomic int *stop, const char *host, unsigned port, unsigned poll_interval_ms, unsigned timeout_ms, const char *log_tag) {
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
    log_line("%s: resolve %s: %s", log_tag, host, gai_strerror(e));
    return -1;
  }
  for (const struct addrinfo *ai = res; ai; ai = ai->ai_next) {
    fd = dial_addr(stop, ai, poll_interval_ms, timeout_ms, &save_errno);
    if (fd >= 0 || cas_dial_stopping(stop)) break;
  }
  freeaddrinfo(res);
  if (fd < 0) {
    if (!cas_dial_stopping(stop)) log_line("%s: connect %s:%u: %s", log_tag, host, port, strerror(save_errno));
    return -1;
  }
  return fd;
}

void cas_interruptible_backoff(const _Atomic int *stop, unsigned ms, unsigned poll_interval_ms) {
  unsigned waited = 0;
  while (waited < ms && !cas_dial_stopping(stop)) {
    unsigned step = (ms - waited < poll_interval_ms) ? ms - waited : poll_interval_ms;
    struct timespec ts = {step / 1000, (long)(step % 1000) * 1000000L};
    nanosleep(&ts, NULL);
    waited += step;
  }
}
