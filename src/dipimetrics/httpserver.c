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

#include "lib/helper/log.h"
#include "lib/vendor/picohttpparser/picohttpparser.h"

#include "httpserver.h"
#include "render.h"

#define HTTP_IDLE_TIMEOUT_S 5 /* reaps stuck/idle peer */
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

typedef struct {
  int used;
  int fd;
  int pfd_idx; /* index into callers pfds[] from last poll_fds() call, -1 = unpolled */
  int reading; /* 1 = still reading request, 0 = sending response */
  double deadline;
  char reqbuf[REQ_BUF_CAP];
  size_t reqlen;
  char *resp;
  size_t resplen, respoff;
} http_conn_t;

struct http_server {
  int listen_fd;
  int listen_pfd_idx;
  http_conn_t conns[HTTP_MAX_CONNS];
};

http_server_t *http_server_new(int listen_fd) {
  http_server_t *hs = calloc(1, sizeof *hs);
  if (!hs)
    return NULL;
  hs->listen_fd = listen_fd;
  hs->listen_pfd_idx = -1;
  return hs;
}

void http_server_free(http_server_t *hs) {
  if (!hs)
    return;
  for (int i = 0; i < HTTP_MAX_CONNS; i++) {
    if (hs->conns[i].used) {
      close(hs->conns[i].fd);
      free(hs->conns[i].resp);
    }
  }
  free(hs);
}

void http_server_poll_fds(http_server_t *hs, struct pollfd *pfds, int cap, int *n) {
  hs->listen_pfd_idx = -1;
  if (*n < cap) {
    hs->listen_pfd_idx = *n;
    pfds[*n].fd = hs->listen_fd;
    pfds[*n].events = POLLIN;
    pfds[*n].revents = 0;
    (*n)++;
  }
  for (int i = 0; i < HTTP_MAX_CONNS; i++) {
    http_conn_t *c = &hs->conns[i];
    if (!c->used) {
      c->pfd_idx = -1;
      continue;
    }
    if (*n >= cap) {
      c->pfd_idx = -1;
      continue;
    }
    c->pfd_idx = *n;
    pfds[*n].fd = c->fd;
    pfds[*n].events = (short)(c->reading ? POLLIN : POLLOUT);
    pfds[*n].revents = 0;
    (*n)++;
  }
}

static void conn_close(http_conn_t *c) {
  close(c->fd);
  free(c->resp);
  memset(c, 0, sizeof *c);
}

static void conn_accept(http_server_t *hs, int fd, double now_mono) {
  int flags;
  http_conn_t *c = NULL;

  for (int i = 0; i < HTTP_MAX_CONNS; i++)
    if (!hs->conns[i].used) {
      c = &hs->conns[i];
      break;
    }
  if (!c) {
    close(fd); /* pool full, drop rather than let it queue up unbounded */
    return;
  }
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    close(fd);
    return;
  }
  memset(c, 0, sizeof *c);
  c->used = 1;
  c->fd = fd;
  c->reading = 1;
  c->pfd_idx = -1;
  c->deadline = now_mono + HTTP_IDLE_TIMEOUT_S;
}

static void conn_read_step(http_conn_t *c) {
  ssize_t got;

  if (c->reqlen >= sizeof c->reqbuf - 1) {
    conn_close(c);
    return;
  }
  got = recv(c->fd, c->reqbuf + c->reqlen, sizeof c->reqbuf - 1 - c->reqlen, 0);
  if (got < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK)
      conn_close(c);
    return;
  }
  if (got == 0) {
    conn_close(c); /* peer closed before sending a full request */
    return;
  }
  c->reqlen += (size_t)got;
  c->reqbuf[c->reqlen] = '\0';
}

static const char *strip_query(char *path) {
  char *q = strchr(path, '?');
  if (q)
    *q = '\0';
  return path;
}

/* combines header+body into one buffer up front: send-side then only tracks one offset */
static void set_response(http_conn_t *c, const char *hdr, size_t hdr_len, const char *body, size_t body_len) {
  c->resp = malloc(hdr_len + body_len);
  if (!c->resp) {
    c->resplen = 0;
    return;
  }
  memcpy(c->resp, hdr, hdr_len);
  if (body_len)
    memcpy(c->resp + hdr_len, body, body_len);
  c->resplen = hdr_len + body_len;
}

static void build_metrics_response(http_conn_t *c, const store_t *st, double now_mono) {
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
  set_response(c, hdr, (size_t)hdr_len, body, body_len);
  free(body);
}

static void build_404_response(http_conn_t *c) {
  static const char body[] = "not found\n";
  char hdr[160];
  int hdr_len = snprintf(hdr, sizeof hdr,
                          "HTTP/1.1 404 Not Found\r\n"
                          "Content-Type: text/plain; charset=utf-8\r\n"
                          "Content-Length: %zu\r\n"
                          "Connection: close\r\n\r\n",
                          sizeof body - 1);
  set_response(c, hdr, (size_t)hdr_len, body, sizeof body - 1);
}

static int request_headers_complete(const char *buf, size_t len) {
  const char *method, *path;
  size_t method_len, path_len;
  int minor_version;
  struct phr_header headers[32];
  size_t num_headers = 32;
  return phr_parse_request(buf, len, &method, &method_len, &path, &path_len, &minor_version, headers, &num_headers, 0) != -2;
}

static void conn_build_response(http_conn_t *c, store_t *st, double now_mono, int verbose) {
  const char *pmethod, *ppath;
  size_t method_len = 0, path_len = 0;
  int minor_version;
  struct phr_header headers[32];
  size_t num_headers = 32;
  char method[16] = "", path[256] = "";

  if (phr_parse_request(c->reqbuf, c->reqlen, &pmethod, &method_len, &ppath, &path_len, &minor_version, headers, &num_headers, 0) > 0) {
    if (method_len >= sizeof method)
      method_len = sizeof method - 1;
    memcpy(method, pmethod, method_len);
    method[method_len] = '\0';
    if (path_len >= sizeof path)
      path_len = sizeof path - 1;
    memcpy(path, ppath, path_len);
    path[path_len] = '\0';
  }

  if (!strcmp(method, "GET") && !strcmp(strip_query(path), "/metrics")) {
    st->stats.http_requests_200++;
    build_metrics_response(c, st, now_mono);
  } else {
    if (verbose && method[0])
      log_line("dipimetrics: 404 %s %s", method, path);
    st->stats.http_requests_404++;
    build_404_response(c);
  }
  c->reading = 0;
  c->respoff = 0;
  c->deadline = now_mono + HTTP_IDLE_TIMEOUT_S;
}

static void conn_write_step(http_conn_t *c) {
  ssize_t sent;

  if (!c->resp || c->respoff >= c->resplen) {
    conn_close(c);
    return;
  }
  sent = send(c->fd, c->resp + c->respoff, c->resplen - c->respoff, MSG_NOSIGNAL);
  if (sent < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK)
      conn_close(c);
    return;
  }
  c->respoff += (size_t)sent;
  if (c->respoff >= c->resplen)
    conn_close(c);
}

void http_server_service(http_server_t *hs, const struct pollfd *pfds, int n, store_t *st, double now_mono, int verbose) {
  if (hs->listen_pfd_idx >= 0 && hs->listen_pfd_idx < n && (pfds[hs->listen_pfd_idx].revents & POLLIN)) {
    for (;;) {
      int fd = accept(hs->listen_fd, NULL, NULL);
      if (fd < 0)
        break;
      conn_accept(hs, fd, now_mono);
    }
  }

  for (int i = 0; i < HTTP_MAX_CONNS; i++) {
    http_conn_t *c = &hs->conns[i];
    short rev;
    if (!c->used)
      continue;
    rev = (c->pfd_idx >= 0 && c->pfd_idx < n) ? pfds[c->pfd_idx].revents : 0;
    if (c->reading) {
      if (rev & (POLLIN | POLLHUP | POLLERR))
        conn_read_step(c);
      if (c->used && c->reading && request_headers_complete(c->reqbuf, c->reqlen))
        conn_build_response(c, st, now_mono, verbose);
    } else if (rev & (POLLOUT | POLLERR)) {
      conn_write_step(c);
    }
    if (c->used && now_mono >= c->deadline)
      conn_close(c);
  }
}
