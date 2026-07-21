/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "../log.h"
#include "httpclient.h"
#include "tls.h"

#define HTTP_HDR_MAX 32
#define HTTP_REDIRECT_MAX 5

struct http {
  int fd;
  tls_t *tls; /* NULL: plain http */
  http_url_t url;
  unsigned char hold[8192]; /* body read with headers, drained first */
  size_t hlen, hpos;
  int status;
  struct {
    char name[64];
    char value[512];
  } hdr[HTTP_HDR_MAX];
  int hdr_count;
};

int http_url_parse(const char *uri, http_url_t *u) {
  const char *p = uri, *host, *rest;
  size_t hostlen;
  memset(u, 0, sizeof *u);
  if (!strncmp(p, "https://", 8)) {
    u->tls = 1;
    u->port = 443;
    p += 8;
  } else if (!strncmp(p, "http://", 7)) {
    u->tls = 0;
    u->port = 80;
    p += 7;
  } else {
    return -1;
  }
  if (!*p)
    return -1;

  host = p;
  rest = strpbrk(p, ":/");
  hostlen = rest ? (size_t)(rest - host) : strlen(host);
  if (hostlen == 0 || hostlen >= sizeof u->host)
    return -1;
  memcpy(u->host, host, hostlen);
  u->host[hostlen] = '\0';

  if (rest && *rest == ':') {
    char *end;
    unsigned long port = strtoul(rest + 1, &end, 10);
    if (end == rest + 1 || port == 0 || port > 65535)
      return -1;
    u->port = (unsigned)port;
    rest = strchr(rest, '/');
  }
  if (rest) {
    if (strlen(rest) >= sizeof u->path)
      return -1;
    strcpy(u->path, rest);
  } else {
    strcpy(u->path, "/");
  }
  return 0;
}

static int tcp_connect(const char *host, unsigned port) {
  struct addrinfo hints, *res, *ai;
  char portstr[6];
  int fd = -1, e;

  snprintf(portstr, sizeof portstr, "%u", port);
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  e = getaddrinfo(host, portstr, &hints, &res);
  if (e) {
    log_line("resolve %s: %s", host, gai_strerror(e));
    return -1;
  }
  for (ai = res; ai; ai = ai->ai_next) {
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0)
      continue;
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
      break;
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd < 0)
    log_line("connect %s:%u: %s", host, port, strerror(errno));
  return fd;
}

static ssize_t raw_recv(struct http *h, void *buf, size_t cap) {
  ssize_t n;

  if (h->tls)
    return tls_read(h->tls, buf, cap);
  n = recv(h->fd, buf, cap, 0);
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return 0;
    log_line("recv: %s", strerror(errno));
    return -1;
  }
  if (n == 0)
    return -1;
  return n;
}

static int raw_send_all(struct http *h, const char *buf, size_t n) {
  while (n) {
    ssize_t w = h->tls ? tls_write(h->tls, buf, n) : send(h->fd, buf, n, MSG_NOSIGNAL);
    if (w < 0) {
      if (!h->tls && (errno == EINTR))
        continue;
      log_line("send: %s", h->tls ? "tls write failed" : strerror(errno));
      return -1;
    }
    if (w == 0)
      continue; /* transient, retry */
    buf += w;
    n -= (size_t)w;
  }
  return 0;
}

static char *find_crlf2(char *b, size_t n) {
  size_t i;
  for (i = 0; i + 3 < n; i++)
    if (b[i] == '\r' && b[i + 1] == '\n' && b[i + 2] == '\r' && b[i + 3] == '\n')
      return b + i;
  return NULL;
}

static void hdr_lower(char *s) {
  for (; *s; s++)
    *s = (char)tolower((unsigned char)*s);
}

static void parse_headers(struct http *h, char *block) {
  char *line, *save = NULL;

  line = strtok_r(block, "\r\n", &save);
  if (!line)
    return;
  { /* status line: HTTP/1.x <code> ... */
    char *sp = strchr(line, ' ');
    h->status = sp ? atoi(sp + 1) : 0;
  }
  while ((line = strtok_r(NULL, "\r\n", &save)) != NULL && h->hdr_count < HTTP_HDR_MAX) {
    char *colon = strchr(line, ':');
    char *v;
    size_t nlen;
    if (!colon)
      continue;
    nlen = (size_t)(colon - line);
    if (nlen >= sizeof h->hdr[0].name)
      nlen = sizeof h->hdr[0].name - 1;
    memcpy(h->hdr[h->hdr_count].name, line, nlen);
    h->hdr[h->hdr_count].name[nlen] = '\0';
    hdr_lower(h->hdr[h->hdr_count].name);
    v = colon + 1;
    while (*v == ' ' || *v == '\t')
      v++;
    snprintf(h->hdr[h->hdr_count].value, sizeof h->hdr[0].value, "%s", v);
    h->hdr_count++;
  }
}

const char *http_header(const http_t *h, const char *name) {
  char key[64];
  int i;

  snprintf(key, sizeof key, "%s", name);
  hdr_lower(key);
  for (i = 0; i < h->hdr_count; i++)
    if (!strcmp(h->hdr[i].name, key))
      return h->hdr[i].value;
  return NULL;
}

const http_url_t *http_final_url(const http_t *h) { return &h->url; }

static int resolve_location(http_url_t *u, const char *loc) {
  if (!strncmp(loc, "http://", 7) || !strncmp(loc, "https://", 8))
    return http_url_parse(loc, u);
  if (loc[0] == '/') {
    if (strlen(loc) >= sizeof u->path)
      return -1;
    strcpy(u->path, loc);
    return 0;
  }
  return -1;
}

static struct http *fetch_once(const http_url_t *url, const char *user_agent, int insecure) {
  struct http *h = calloc(1, sizeof *h);
  struct timeval tv = {5, 0};
  char req[1200];
  int rl;
  size_t got = 0;
  char *term;

  if (!h)
    return NULL;
  h->url = *url;
  h->fd = tcp_connect(h->url.host, h->url.port);
  if (h->fd < 0) {
    free(h);
    return NULL;
  }
  setsockopt(h->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  if (h->url.tls) {
    h->tls = tls_connect(h->fd, h->url.host, insecure);
    if (!h->tls) {
      close(h->fd);
      free(h);
      return NULL;
    }
  }

  rl = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nIcy-MetaData: 1\r\nConnection: close\r\n\r\n", h->url.path, h->url.host, user_agent);
  if (rl < 0 || rl >= (int)sizeof req) {
    log_line("http request too long");
    goto fail;
  }
  if (raw_send_all(h, req, (size_t)rl))
    goto fail;
  while (got < sizeof h->hold) {
    ssize_t n = raw_recv(h, h->hold + got, sizeof h->hold - got);
    if (n < 0) {
      log_line("http: connection closed while reading headers");
      goto fail;
    }
    if (n == 0) {
      log_line("http: timed out waiting for response headers");
      goto fail;
    }
    got += (size_t)n;
    term = find_crlf2((char *)h->hold, got);
    if (term)
      break;
  }
  if (got >= sizeof h->hold) {
    log_line("http: response headers too large");
    goto fail;
  }
  term = find_crlf2((char *)h->hold, got);
  if (!term) {
    log_line("http: no header terminator");
    goto fail;
  }
  {
    size_t hdrlen = (size_t)(term - (char *)h->hold);
    size_t consumed = hdrlen + 4;
    char block[sizeof h->hold];
    memcpy(block, h->hold, hdrlen);
    block[hdrlen] = '\0';
    parse_headers(h, block);
    h->hlen = got - consumed;
    memmove(h->hold, h->hold + consumed, h->hlen);
    h->hpos = 0;
  }
  return h;

fail:
  if (h->tls)
    tls_close(h->tls);
  else if (h->fd >= 0)
    close(h->fd);
  free(h);
  return NULL;
}

http_t *http_get(const http_url_t *url_in, const char *user_agent, int insecure) {
  http_url_t url = *url_in;
  int redirects;
  const char *ua = user_agent ? user_agent : "dvbipitools";

  for (redirects = 0; redirects <= HTTP_REDIRECT_MAX; redirects++) {
    struct http *h = fetch_once(&url, ua, insecure);
    if (!h)
      return NULL;
    if ((h->status == 301 || h->status == 302 || h->status == 303 || h->status == 307 || h->status == 308) && redirects < HTTP_REDIRECT_MAX) {
      const char *loc = http_header(h, "location");
      http_url_t next = url;
      if (!loc || resolve_location(&next, loc) != 0) {
        log_line("http: redirect without usable Location");
        http_close(h);
        return NULL;
      }
      http_close(h);
      url = next;
      continue;
    }
    if (h->status < 200 || h->status >= 300) {
      log_line("http %d fetching %s%s", h->status, h->url.host, h->url.path);
      http_close(h);
      return NULL;
    }
    return h;
  }
  log_line("http: too many redirects");
  return NULL;
}

ssize_t http_read(http_t *h, void *buf, size_t cap) {
  if (h->hpos < h->hlen) {
    size_t k = h->hlen - h->hpos;
    if (k > cap)
      k = cap;
    memcpy(buf, h->hold + h->hpos, k);
    h->hpos += k;
    return (ssize_t)k;
  }
  return raw_recv(h, buf, cap);
}

void http_close(http_t *h) {
  if (!h)
    return;
  if (h->tls)
    tls_close(h->tls);
  else if (h->fd >= 0)
    close(h->fd);
  free(h);
}
