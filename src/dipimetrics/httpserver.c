/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/log.h"
#include "lib/signal.h"

#include "httpserver.h"
#include "render.h"

#define HTTP_IO_TIMEOUT_S 5
#define REQ_BUF_CAP 8192

int http_listen(int family, const char *addr, unsigned port) {
  int fd, on = 1, flags;
  struct sockaddr_storage ss;
  socklen_t sslen;

  fd = socket(family, SOCK_STREAM, 0);
  if (fd < 0) {
    log_line("dipimetrics: socket() failed: %s", strerror(errno));
    return -1;
  }
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);

  memset(&ss, 0, sizeof ss);
  if (family == AF_INET6) {
    struct sockaddr_in6 *a = (struct sockaddr_in6 *)&ss;
    a->sin6_family = AF_INET6;
    a->sin6_port = htons((unsigned short)port);
    if (inet_pton(AF_INET6, addr, &a->sin6_addr) != 1) {
      log_line("dipimetrics: invalid listen address %s", addr);
      close(fd);
      return -1;
    }
    sslen = sizeof *a;
  } else {
    struct sockaddr_in *a = (struct sockaddr_in *)&ss;
    a->sin_family = AF_INET;
    a->sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, addr, &a->sin_addr) != 1) {
      log_line("dipimetrics: invalid listen address %s", addr);
      close(fd);
      return -1;
    }
    sslen = sizeof *a;
  }

  if (bind(fd, (struct sockaddr *)&ss, sslen) < 0) {
    log_line("dipimetrics: bind %s:%u failed: %s", addr, port, strerror(errno));
    close(fd);
    return -1;
  }
  if (listen(fd, 16) < 0) {
    log_line("dipimetrics: listen failed: %s", strerror(errno));
    close(fd);
    return -1;
  }
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    log_line("dipimetrics: fcntl O_NONBLOCK: %s", strerror(errno));
    close(fd);
    return -1;
  }
  return fd;
}

static int write_all(int fd, const char *buf, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
    if (n <= 0)
      return -1;
    sent += (size_t)n;
  }
  return 0;
}

static void respond_metrics(int fd, const store_t *st, double now_mono) {
  char *body;
  size_t body_len;
  char hdr[256];
  int hdr_len;

  render_openmetrics(st, now_mono, &body, &body_len);
  hdr_len = snprintf(hdr, sizeof hdr,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/openmetrics-text; version=1.0.0; charset=utf-8\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n\r\n",
                      body_len);
  if (write_all(fd, hdr, (size_t)hdr_len) == 0)
    write_all(fd, body, body_len);
  free(body);
}

static void respond_404(int fd) {
  static const char body[] = "not found\n";
  char hdr[160];
  int hdr_len = snprintf(hdr, sizeof hdr,
                          "HTTP/1.1 404 Not Found\r\n"
                          "Content-Type: text/plain; charset=utf-8\r\n"
                          "Content-Length: %zu\r\n"
                          "Connection: close\r\n\r\n",
                          sizeof body - 1);
  if (write_all(fd, hdr, (size_t)hdr_len) == 0)
    write_all(fd, body, sizeof body - 1);
}

static const char *strip_query(char *path) {
  char *q = strchr(path, '?');
  if (q)
    *q = '\0';
  return path;
}

void http_accept_and_serve(int listen_fd, store_t *st, double now_mono, int verbose) {
  int fd;
  struct timeval tv;
  char reqbuf[REQ_BUF_CAP];
  size_t reqlen = 0;
  double deadline;
  char method[16], path[256];

  fd = accept(listen_fd, NULL, NULL);
  if (fd < 0)
    return;

  tv.tv_sec = HTTP_IO_TIMEOUT_S;
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

  reqbuf[0] = '\0';
  deadline = mono_seconds() + HTTP_IO_TIMEOUT_S;
  for (;;) {
    ssize_t n;
    if (reqlen >= sizeof reqbuf - 1 || mono_seconds() >= deadline)
      break;
    n = recv(fd, reqbuf + reqlen, sizeof reqbuf - 1 - reqlen, 0);
    if (n <= 0)
      break;
    reqlen += (size_t)n;
    reqbuf[reqlen] = '\0';
    if (strstr(reqbuf, "\r\n\r\n"))
      break;
  }

  method[0] = '\0';
  path[0] = '\0';
  sscanf(reqbuf, "%15s %255s", method, path);

  if (!strcmp(method, "GET") && !strcmp(strip_query(path), "/metrics")) {
    st->stats.http_requests_200++;
    respond_metrics(fd, st, now_mono);
  } else {
    if (verbose && method[0])
      log_line("dipimetrics: 404 %s %s", method, path);
    st->stats.http_requests_404++;
    respond_404(fd);
  }
  close(fd);
}
