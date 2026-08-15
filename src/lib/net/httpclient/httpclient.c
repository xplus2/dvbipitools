/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "../../ioutil.h"
#include "../../log.h"
#include "../../signal.h"
#include "../netconnect.h"
#include "priv.h"

static void set_rcvtimeo(int fd, double secs) {
  struct timeval tv;
  if (secs < 0)
    secs = 0;
  tv.tv_sec = (time_t)secs;
  tv.tv_usec = (suseconds_t)((secs - (double)tv.tv_sec) * 1e6);
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}

static int tcp_connect(const char *host, unsigned port, net_err_reason_t *reason_out) {
  int fd = netconnect_tcp(host, port, HTTP_CONNECT_TIMEOUT_MS, reason_out);
  int flags;

  if (fd < 0)
    return -1;
  /* clear O_NONBLOCK: raw_recv/raw_send_all expect a blocking socket paced by SO_RCVTIMEO */
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
    log_line("httpclient: fcntl O_NONBLOCK: %s", strerror(errno));
    if (reason_out)
      *reason_out = NET_ERR_OTHER;
    close(fd);
    return -1;
  }
  return fd;
}

ssize_t raw_recv(struct http *h, void *buf, size_t cap, net_err_reason_t *reason_out) {
  ssize_t n;

  if (h->tls) {
    n = tls_read(h->tls, buf, cap);
    if (n < 0 && reason_out)
      *reason_out = NET_ERR_TLS;
    return n;
  }
  for (;;) {
    n = recv(h->fd, buf, cap, 0);
    if (n < 0) {
      if (errno == EINTR)
        continue; /* spurious wakeup, not a real timeout; retry transparently */
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return 0;
      log_line("recv: %s", strerror(errno));
      if (reason_out)
        *reason_out = NET_ERR_READ;
      return -1;
    }
    if (n == 0) {
      if (reason_out)
        *reason_out = NET_ERR_EOF;
      return -1;
    }
    return n;
  }
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

/* end of headers: CRLFCRLF or bare LFLF (some servers skip \r). termlen: 4 or 2 */
char *find_header_end(char *b, size_t n, size_t *termlen) {
  size_t i;
  for (i = 0; i + 1 < n; i++) {
    if (b[i] == '\n' && b[i + 1] == '\n') {
      *termlen = 2;
      return b + i;
    }
    if (i + 3 < n && b[i] == '\r' && b[i + 1] == '\n' && b[i + 2] == '\r' && b[i + 3] == '\n') {
      *termlen = 4;
      return b + i;
    }
  }
  return NULL;
}

static void hdr_lower(char *s) {
  for (; *s; s++)
    *s = (char)tolower((unsigned char)*s);
}

void parse_headers(struct http *h, char *block) {
  char *line, *save = NULL;

  line = strtok_r(block, "\r\n", &save);
  if (!line)
    return;
  { /* status line: HTTP/1.x <code> ... */
    char *sp = strchr(line, ' ');
    h->status = sp ? atoi(sp + 1) : 0;
  }
  while ((line = strtok_r(NULL, "\r\n", &save)) != NULL) {
    char *colon = strchr(line, ':');
    char *v;
    size_t nlen;
    if (!colon)
      continue;
    if (h->hdr_count >= HTTP_HDR_MAX) {
      log_line("http: response has more than %d headers, dropping rest", HTTP_HDR_MAX);
      break;
    }
    nlen = (size_t)(colon - line);
    if (nlen >= sizeof h->hdr[0].name)
      nlen = sizeof h->hdr[0].name - 1;
    memcpy(h->hdr[h->hdr_count].name, line, nlen);
    h->hdr[h->hdr_count].name[nlen] = '\0';
    hdr_lower(h->hdr[h->hdr_count].name);
    v = colon + 1;
    while (*v == ' ' || *v == '\t')
      v++;
    bufcpy(h->hdr[h->hdr_count].value, sizeof h->hdr[0].value, v);
    h->hdr_count++;
  }
}

static int transfer_encoding_is_chunked(const char *v) {
  size_t n = strlen(v);
  while (n && (v[n - 1] == ' ' || v[n - 1] == '\t'))
    n--;
  return n == 7 && !strncasecmp(v, "chunked", 7);
}

/* rejects any other transfer-coding (gzip, compress, identity, ...); none supported here */
int setup_transfer_encoding(struct http *h, net_err_reason_t *reason_out) {
  const char *te = http_header(h, "transfer-encoding");
  if (!te)
    return 0;
  if (!transfer_encoding_is_chunked(te)) {
    log_line("http: unsupported transfer-encoding: %s", te);
    if (reason_out)
      *reason_out = NET_ERR_FORMAT;
    return -1;
  }
  h->chunked = 1;
  h->cstate = CHUNK_SIZE_LINE;
  return 0;
}

const char *http_header(const http_t *h, const char *name) {
  char key[64];
  int i;

  bufcpy(key, sizeof key, name);
  hdr_lower(key);
  for (i = 0; i < h->hdr_count; i++)
    if (!strcmp(h->hdr[i].name, key))
      return h->hdr[i].value;
  return NULL;
}

const http_url_t *http_final_url(const http_t *h) { return &h->url; }

int http_status(const http_t *h) { return h->status; }

static struct http *fetch_once(const http_url_t *url, const char *user_agent, int insecure, const char *extra_header, net_err_reason_t *reason_out) {
  struct http *h = calloc(1, sizeof *h);
  char req[2048], hdrline[512] = "";
  int rl;
  size_t got = 0;
  char *term;

  if (!h)
    return NULL;
  h->url = *url;
  h->fd = tcp_connect(h->url.host, h->url.port, reason_out);
  if (h->fd < 0) {
    free(h);
    return NULL;
  }
  set_rcvtimeo(h->fd, (double)HTTP_HEADER_TIMEOUT_MS / 1000.0);
  if (h->url.tls) {
    h->tls = tls_connect(h->fd, h->url.host, insecure);
    if (!h->tls) {
      close(h->fd);
      free(h);
      if (reason_out)
        *reason_out = NET_ERR_TLS;
      return NULL;
    }
  }

  if (extra_header)
    snprintf(hdrline, sizeof hdrline, "%s\r\n", extra_header);
  {
    char hostport[300];
    unsigned default_port = h->url.tls ? 443 : 80;
    if (h->url.port == default_port)
      bufcpy(hostport, sizeof hostport, h->url.host);
    else
      snprintf(hostport, sizeof hostport, "%s:%u", h->url.host, h->url.port);
    rl = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nIcy-MetaData: 1\r\n%sConnection: close\r\n\r\n", h->url.path, hostport, user_agent, hdrline);
  }
  if (rl < 0 || rl >= (int)sizeof req) {
    log_line("http request too long");
    if (reason_out)
      *reason_out = NET_ERR_FORMAT;
    goto fail;
  }
  if (raw_send_all(h, req, (size_t)rl)) {
    if (reason_out)
      *reason_out = NET_ERR_READ;
    goto fail;
  }
  {
    size_t termlen = 0;
    double deadline = mono_seconds() + (double)HTTP_HEADER_TIMEOUT_MS / 1000.0;
    while (got < sizeof h->hold) {
      double remain = deadline - mono_seconds();
      ssize_t n;
      if (remain <= 0) {
        log_line("http: timed out waiting for response headers");
        if (reason_out)
          *reason_out = NET_ERR_TIMEOUT;
        goto fail;
      }
      set_rcvtimeo(h->fd, remain);
      n = raw_recv(h, h->hold + got, sizeof h->hold - got, reason_out);
      if (n < 0) {
        log_line("http: connection closed while reading headers");
        goto fail;
      }
      if (n == 0) {
        log_line("http: timed out waiting for response headers");
        if (reason_out)
          *reason_out = NET_ERR_TIMEOUT;
        goto fail;
      }
      got += (size_t)n;
      term = find_header_end((char *)h->hold, got, &termlen);
      if (term)
        break;
    }
    set_rcvtimeo(h->fd, (double)HTTP_HEADER_TIMEOUT_MS / 1000.0);
    if (!term) {
      log_line("http: response headers too large");
      if (reason_out)
        *reason_out = NET_ERR_FORMAT;
      goto fail;
    }
    term = find_header_end((char *)h->hold, got, &termlen);
    if (!term) {
      log_line("http: no header terminator");
      if (reason_out)
        *reason_out = NET_ERR_FORMAT;
      goto fail;
    }
    {
      size_t hdrlen = (size_t)(term - (char *)h->hold);
      size_t consumed = hdrlen + termlen;
      char block[sizeof h->hold];
      memcpy(block, h->hold, hdrlen);
      block[hdrlen] = '\0';
      parse_headers(h, block);
      h->hlen = got - consumed;
      memmove(h->hold, h->hold + consumed, h->hlen);
      h->hpos = 0;
    }
  }
  if (setup_transfer_encoding(h, reason_out) != 0)
    goto fail;
  return h;

fail:
  if (h->tls)
    tls_close(h->tls);
  else if (h->fd >= 0)
    close(h->fd);
  free(h);
  return NULL;
}

/* resolves the redirect target from Location header. 0 ok, -1 failed (reason_out set) */
static int resolve_redirect(struct http *h, http_url_t *next, net_err_reason_t *reason_out) {
  const char *loc = http_header(h, "location");
  if (!loc || resolve_location(next, loc) != 0) {
    log_line("http: redirect without usable Location");
    if (reason_out)
      *reason_out = NET_ERR_FORMAT;
    return -1;
  }
  return 0;
}

http_t *http_get(const http_url_t *url_in, const char *user_agent, int insecure, const char *extra_header, net_err_reason_t *reason_out) {
  http_url_t url = *url_in;
  int redirects;
  const char *ua = user_agent ? user_agent : "dvbipitools";

  for (redirects = 0; redirects <= HTTP_REDIRECT_MAX; redirects++) {
    struct http *h = fetch_once(&url, ua, insecure, extra_header, reason_out);
    if (!h)
      return NULL;
    if ((h->status == 301 || h->status == 302 || h->status == 303 || h->status == 307 || h->status == 308) && redirects < HTTP_REDIRECT_MAX) {
      http_url_t next = url;
      if (resolve_redirect(h, &next, reason_out) != 0) {
        http_close(h);
        return NULL;
      }
      http_close(h);
      url = next;
      continue;
    }
    if (h->status == 304)
      return h; /* not-modified: caller checks http_status(), body is empty */
    if (h->status < 200 || h->status >= 300) {
      log_line("http %d fetching %s%s", h->status, h->url.host, h->url.path);
      http_close(h);
      if (reason_out)
        *reason_out = NET_ERR_HTTP;
      return NULL;
    }
    return h;
  }
  log_line("http: too many redirects");
  if (reason_out)
    *reason_out = NET_ERR_HTTP;
  return NULL;
}
