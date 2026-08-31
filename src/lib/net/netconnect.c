/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../helper/ioutil.h"
#include "../helper/log.h"
#include "../helper/signal.h"

#include "netconnect.h"

#define NETCONNECT_POLL_INTERVAL_MS 150

const char *net_err_reason_name(net_err_reason_t reason) {
  static const char *const names[NET_ERR_COUNT] = {
      "dns", "connect", "timeout", "tls", "http", "format", "read", "eof", "other"};
  if ((unsigned)reason >= NET_ERR_COUNT)
    return "other";
  return names[reason];
}

static void set_reason(net_err_reason_t *out, net_err_reason_t v) {
  if (out)
    *out = v;
}

/* 1 connected, -1 refused/error, 0 timed out or stop requested */
static int wait_connect(int fd, int timeout_ms) {
  int elapsed = 0;

  while (elapsed < timeout_ms) {
    struct pollfd pfd;
    int step = NETCONNECT_POLL_INTERVAL_MS;
    int pret;

    if (signal_stop_requested())
      return 0;
    if (step > timeout_ms - elapsed)
      step = timeout_ms - elapsed;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    pret = poll(&pfd, 1, step);
    if (pret > 0) {
      int soerr = 0;
      socklen_t sl = sizeof soerr;
      getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
      if (soerr == 0)
        return 1;
      errno = soerr;
      return -1;
    }
    if (pret < 0 && errno != EINTR)
      return -1;
    elapsed += step;
  }
  return 0;
}

int netconnect_tcp(const char *host, unsigned port, int timeout_ms, net_err_reason_t *reason_out) {
  struct addrinfo hints, *res, *ai;
  char portstr[6];
  int fd = -1, e, save_errno = 0;

  uint_to_str(portstr, port);
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  e = getaddrinfo(host, portstr, &hints, &res);
  if (e) {
    log_line("resolve %s: %s", host, gai_strerror(e));
    set_reason(reason_out, NET_ERR_DNS);
    return -1;
  }
  for (ai = res; ai; ai = ai->ai_next) {
    int flags, cr;
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0)
      continue;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      save_errno = errno;
      close(fd);
      fd = -1;
      continue;
    }
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
      break;
    if (errno != EINPROGRESS) {
      save_errno = errno;
      close(fd);
      fd = -1;
      continue;
    }
    cr = wait_connect(fd, timeout_ms);
    if (cr == 1)
      break;
    save_errno = (cr == -1) ? errno : ETIMEDOUT;
    close(fd);
    fd = -1;
    if (signal_stop_requested())
      break;
  }
  freeaddrinfo(res);
  if (fd < 0) {
    if (!signal_stop_requested())
      log_line("connect %s:%u: %s", host, port, strerror(save_errno));
    set_reason(reason_out, (save_errno == ETIMEDOUT) ? NET_ERR_TIMEOUT : NET_ERR_CONNECT);
    return -1;
  }
  return fd; /* already O_NONBLOCK, set before connect() */
}

struct netconnect_pending {
  struct addrinfo *res, *ai; /* res: full list, for freeaddrinfo(). ai: candidate fd is connecting on */
  char host[256];
  unsigned port;
};

/* tries ai onward, first usable candidate wins. -1 if none work (save_errno set) */
static int try_addrs(struct addrinfo *ai, struct addrinfo **used, int *save_errno) {
  for (; ai; ai = ai->ai_next) {
    int fd, flags;
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0)
      continue;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      *save_errno = errno;
      close(fd);
      continue;
    }
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
      *used = ai;
      return fd; /* rare, e.g. loopback: already connected */
    }
    if (errno == EINPROGRESS) {
      *used = ai;
      return fd; /* caller polls POLLOUT, then netconnect_tcp_finish() */
    }
    *save_errno = errno;
    close(fd);
  }
  return -1;
}

int netconnect_tcp_start(const char *host, unsigned port, netconnect_pending_t **pending_out, net_err_reason_t *reason_out) {
  struct addrinfo hints, *res, *used;
  char portstr[6];
  int fd, e, save_errno = 0;
  netconnect_pending_t *p;

  uint_to_str(portstr, port);
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  e = getaddrinfo(host, portstr, &hints, &res);
  if (e) {
    log_line("resolve %s: %s", host, gai_strerror(e));
    set_reason(reason_out, NET_ERR_DNS);
    return -1;
  }
  fd = try_addrs(res, &used, &save_errno);
  if (fd < 0) {
    freeaddrinfo(res);
    log_line("connect %s:%u: %s", host, port, strerror(save_errno));
    set_reason(reason_out, NET_ERR_CONNECT);
    return -1;
  }
  p = malloc(sizeof *p);
  if (!p) {
    close(fd);
    freeaddrinfo(res);
    set_reason(reason_out, NET_ERR_OTHER);
    return -1;
  }
  p->res = res;
  p->ai = used;
  bufcpy(p->host, sizeof p->host, host);
  p->port = port;
  *pending_out = p;
  return fd;
}

int netconnect_tcp_finish(netconnect_pending_t **pending, int *fd, net_err_reason_t *reason_out) {
  netconnect_pending_t *p = *pending;
  int soerr = 0, save_errno;
  socklen_t sl = sizeof soerr;
  struct addrinfo *used;
  int nfd;

  if (getsockopt(*fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) < 0) {
    save_errno = errno;
    close(*fd);
    *fd = -1;
    freeaddrinfo(p->res);
    free(p);
    *pending = NULL;
    errno = save_errno;
    set_reason(reason_out, NET_ERR_OTHER);
    return -1;
  }
  if (soerr == 0) {
    freeaddrinfo(p->res);
    free(p);
    *pending = NULL;
    return 1;
  }
  close(*fd);
  save_errno = soerr;
  nfd = try_addrs(p->ai->ai_next, &used, &save_errno);
  if (nfd >= 0) {
    p->ai = used;
    *fd = nfd;
    return 0;
  }
  *fd = -1;
  freeaddrinfo(p->res);
  free(p);
  *pending = NULL;
  errno = save_errno;
  set_reason(reason_out, NET_ERR_CONNECT);
  return -1;
}

void netconnect_tcp_abort(netconnect_pending_t *pending) {
  if (!pending)
    return;
  freeaddrinfo(pending->res);
  free(pending);
}

int netaddr_fill(int family, const char *addr, unsigned port, struct sockaddr_storage *ss, socklen_t *sslen) {
  memset(ss, 0, sizeof *ss);
  if (family == AF_INET) {
    struct sockaddr_in *a = (struct sockaddr_in *)ss;
    a->sin_family = AF_INET;
    a->sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, addr, &a->sin_addr) != 1)
      return -1;
    *sslen = sizeof *a;
  } else {
    struct sockaddr_in6 *a = (struct sockaddr_in6 *)ss;
    a->sin6_family = AF_INET6;
    a->sin6_port = htons((unsigned short)port);
    if (inet_pton(AF_INET6, addr, &a->sin6_addr) != 1)
      return -1;
    *sslen = sizeof *a;
  }
  return 0;
}

int net_set_dscp(int fd, int family, int tos) {
  if (family == AF_INET)
    return setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof tos);
  return setsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof tos);
}
