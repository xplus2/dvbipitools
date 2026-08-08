/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "../log.h"
#include "netconnect.h"
#include "udpxy.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define UDPXY_CONNECT_TIMEOUT_MS 1000

struct udpxy {
  int fd;
  unsigned char hold[8192]; /* body read with headers */
  size_t hlen, hpos;
};

static char *find_crlf2(char *b, size_t n) {
  size_t i;
  for (i = 0; i + 3 < n; i++)
    if (b[i] == '\r' && b[i + 1] == '\n' && b[i + 2] == '\r' &&
        b[i + 3] == '\n')
      return b + i;
  return NULL;
}

static int send_all(int fd, const char *b, size_t n) {
  while (n) {
    ssize_t w = send(fd, b, n, MSG_NOSIGNAL);
    if (w < 0) {
      if (errno == EINTR)
        continue;
      log_line("udpxy send: %s", strerror(errno));
      return -1;
    }
    b += w;
    n -= (size_t)w;
  }
  return 0;
}

udpxy_t *udpxy_open(const char *host, unsigned port, const char *path, const char *user_agent, net_err_reason_t *reason_out) {
  struct timeval tv = {1, 0};
  char req[700];
  udpxy_t *u = NULL;
  int fd, rl, flags;
  size_t got = 0, hdr;
  char *term = NULL;

  fd = netconnect_tcp(host, port, UDPXY_CONNECT_TIMEOUT_MS, reason_out);
  if (fd < 0)
    return NULL;
  /* clear O_NONBLOCK: recv() loop below expects a blocking socket paced by SO_RCVTIMEO */
  flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  u = calloc(1, sizeof *u);
  if (!u)
    goto fail;
  u->fd = fd;

  rl = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n", path, host, user_agent);
  if (rl < 0 || rl >= (int)sizeof req) {
    log_line("udpxy request too long");
    if (reason_out)
      *reason_out = NET_ERR_FORMAT;
    goto fail;
  }
  if (send_all(fd, req, (size_t)rl)) {
    if (reason_out)
      *reason_out = NET_ERR_READ;
    goto fail;
  }

  while (got < sizeof u->hold) {
    ssize_t n = recv(fd, u->hold + got, sizeof u->hold - got, 0);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      log_line("udpxy recv: %s", strerror(errno));
      if (reason_out)
        *reason_out = NET_ERR_READ;
      goto fail;
    }
    if (n == 0) {
      log_line("udpxy closed during headers");
      if (reason_out)
        *reason_out = NET_ERR_EOF;
      goto fail;
    }
    got += (size_t)n;
    term = find_crlf2((char *)u->hold, got);
    if (term)
      break;
  }
  if (!term) {
    log_line("udpxy headers too large");
    if (reason_out)
      *reason_out = NET_ERR_FORMAT;
    goto fail;
  }
  if (got < 12 || memcmp(u->hold, "HTTP/", 5) != 0) {
    log_line("udpxy: malformed response");
    if (reason_out)
      *reason_out = NET_ERR_FORMAT;
    goto fail;
  }
  {
    const char *sp = memchr(u->hold, ' ', (size_t)(term - (char *)u->hold));
    if (!sp || sp[1] != '2') {
      log_line("udpxy: non-2xx status");
      if (reason_out)
        *reason_out = NET_ERR_HTTP;
      goto fail;
    }
  }
  hdr = (size_t)(term - (char *)u->hold) + 4;
  u->hpos = 0;
  u->hlen = got - hdr;
  memmove(u->hold, u->hold + hdr, u->hlen);
  return u;

fail:
  if (fd >= 0)
    close(fd);
  free(u);
  return NULL;
}

ssize_t udpxy_read(udpxy_t *u, void *buf, size_t cap, net_err_reason_t *reason_out) {
  ssize_t n;
  if (u->hpos < u->hlen) {
    size_t k = u->hlen - u->hpos;
    if (k > cap)
      k = cap;
    memcpy(buf, u->hold + u->hpos, k);
    u->hpos += k;
    return (ssize_t)k;
  }
  n = recv(u->fd, buf, cap, 0);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return 0;
    log_line("udpxy recv: %s", strerror(errno));
    if (reason_out)
      *reason_out = NET_ERR_READ;
    return -1;
  }
  if (n == 0) { /* server closed */
    if (reason_out)
      *reason_out = NET_ERR_EOF;
    return -1;
  }
  return n;
}

void udpxy_close(udpxy_t *u) {
  if (!u)
    return;
  if (u->fd >= 0)
    close(u->fd);
  free(u);
}
