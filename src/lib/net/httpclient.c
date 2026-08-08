/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "../ioutil.h"
#include "../log.h"
#include "httpclient.h"
#include "netconnect.h"
#include "tls.h"

#define HTTP_HDR_MAX 32
#define HTTP_REDIRECT_MAX 5
#define HTTP_CONNECT_TIMEOUT_MS 5000

/* chunked transfer-coding (RFC 7230 4.1) decode state, driven byte-by-byte for the
   framing (size line, CRLFs, trailer) and in bulk for chunk data itself */
typedef enum { CHUNK_SIZE_LINE, CHUNK_DATA, CHUNK_DATA_CRLF, CHUNK_TRAILER, CHUNK_DONE } chunk_state_t;

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
  int chunked;
  chunk_state_t cstate;
  unsigned long long chunk_remaining;
  char sizeline[64];
  size_t sizeline_len;
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

static int tcp_connect(const char *host, unsigned port, net_err_reason_t *reason_out) {
  int fd = netconnect_tcp(host, port, HTTP_CONNECT_TIMEOUT_MS, reason_out);
  int flags;

  if (fd < 0)
    return -1;
  /* clear O_NONBLOCK: raw_recv/raw_send_all expect a blocking socket paced by SO_RCVTIMEO */
  flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
  return fd;
}

static ssize_t raw_recv(struct http *h, void *buf, size_t cap, net_err_reason_t *reason_out) {
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
        continue; /* spurious wakeup, not a real timeout - retry transparently */
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
static char *find_header_end(char *b, size_t n, size_t *termlen) {
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

/* rejects any other transfer-coding (gzip, compress, identity, ...) - none supported here */
static int setup_transfer_encoding(struct http *h, net_err_reason_t *reason_out) {
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

static struct http *fetch_once(const http_url_t *url, const char *user_agent, int insecure, const char *extra_header, net_err_reason_t *reason_out) {
  struct http *h = calloc(1, sizeof *h);
  struct timeval tv = {5, 0};
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
  setsockopt(h->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
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
    while (got < sizeof h->hold) {
      ssize_t n = raw_recv(h, h->hold + got, sizeof h->hold - got, reason_out);
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

http_t *http_get(const http_url_t *url_in, const char *user_agent, int insecure, const char *extra_header, net_err_reason_t *reason_out) {
  http_url_t url = *url_in;
  int redirects;
  const char *ua = user_agent ? user_agent : "dvbipitools";

  for (redirects = 0; redirects <= HTTP_REDIRECT_MAX; redirects++) {
    struct http *h = fetch_once(&url, ua, insecure, extra_header, reason_out);
    if (!h)
      return NULL;
    if ((h->status == 301 || h->status == 302 || h->status == 303 || h->status == 307 || h->status == 308) && redirects < HTTP_REDIRECT_MAX) {
      const char *loc = http_header(h, "location");
      http_url_t next = url;
      if (!loc || resolve_location(&next, loc) != 0) {
        log_line("http: redirect without usable Location");
        http_close(h);
        if (reason_out)
          *reason_out = NET_ERR_FORMAT;
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

static ssize_t body_read_raw(struct http *h, void *buf, size_t cap, net_err_reason_t *reason_out) {
  if (h->hpos < h->hlen) {
    size_t k = h->hlen - h->hpos;
    if (k > cap)
      k = cap;
    memcpy(buf, h->hold + h->hpos, k);
    h->hpos += k;
    return (ssize_t)k;
  }
  /* small cap: buffer via hold, 1 recv() not N (chunk framing bytes). big cap: bypass. */
  if (cap < sizeof h->hold) {
    ssize_t n = raw_recv(h, h->hold, sizeof h->hold, reason_out);
    size_t k;
    if (n <= 0)
      return n;
    h->hlen = (size_t)n;
    k = h->hlen < cap ? h->hlen : cap;
    memcpy(buf, h->hold, k);
    h->hpos = k;
    return (ssize_t)k;
  }
  return raw_recv(h, buf, cap, reason_out);
}

static ssize_t http_read_chunked(struct http *h, void *buf, size_t cap, net_err_reason_t *reason_out) {
  unsigned char *out = buf;
  size_t produced = 0;

  while (produced < cap) {
    if (h->cstate == CHUNK_DONE) {
      if (produced)
        break;
      if (reason_out)
        *reason_out = NET_ERR_EOF;
      return -1;
    }

    if (h->cstate == CHUNK_DATA) {
      size_t want = cap - produced;
      ssize_t n;
      if ((unsigned long long)want > h->chunk_remaining)
        want = (size_t)h->chunk_remaining;
      n = body_read_raw(h, out + produced, want, reason_out);
      if (n < 0)
        return produced ? (ssize_t)produced : -1;
      if (n == 0)
        return produced ? (ssize_t)produced : 0;
      produced += (size_t)n;
      h->chunk_remaining -= (unsigned long long)n;
      if (h->chunk_remaining == 0)
        h->cstate = CHUNK_DATA_CRLF;
      continue;
    }

    { /* CHUNK_SIZE_LINE / CHUNK_DATA_CRLF / CHUNK_TRAILER: framing bytes, one at a time */
      unsigned char c;
      ssize_t n = body_read_raw(h, &c, 1, reason_out);
      if (n < 0)
        return produced ? (ssize_t)produced : -1;
      if (n == 0)
        return produced ? (ssize_t)produced : 0;

      if (h->cstate == CHUNK_DATA_CRLF) {
        if (c == '\n')
          h->cstate = CHUNK_SIZE_LINE;
        continue; /* drop CR (and any stray byte) until the LF shows up */
      }

      if (h->cstate == CHUNK_SIZE_LINE) {
        if (c == '\n') {
          char *end;
          unsigned long long size;
          h->sizeline[h->sizeline_len < sizeof h->sizeline ? h->sizeline_len : sizeof h->sizeline - 1] = '\0';
          size = strtoull(h->sizeline, &end, 16);
          if (end == h->sizeline) {
            log_line("http: malformed chunk size line");
            if (reason_out)
              *reason_out = NET_ERR_FORMAT;
            return -1;
          }
          h->chunk_remaining = size;
          h->sizeline_len = 0;
          h->cstate = size == 0 ? CHUNK_TRAILER : CHUNK_DATA;
          continue;
        }
        if (c != '\r' && h->sizeline_len < sizeof h->sizeline - 1)
          h->sizeline[h->sizeline_len++] = (char)c;
        continue;
      }

      /* CHUNK_TRAILER: optional trailer headers after the 0-size chunk, ended by a blank line */
      if (c == '\n') {
        if (h->sizeline_len == 0)
          h->cstate = CHUNK_DONE;
        else
          h->sizeline_len = 0;
      } else if (c != '\r') {
        h->sizeline_len = 1;
      }
    }
  }
  return (ssize_t)produced;
}

ssize_t http_read(http_t *h, void *buf, size_t cap, net_err_reason_t *reason_out) {
  if (h->chunked)
    return http_read_chunked(h, buf, cap, reason_out);
  return body_read_raw(h, buf, cap, reason_out);
}

int http_fd(const http_t *h) { return h->fd; }

void http_close(http_t *h) {
  if (!h)
    return;
  if (h->tls)
    tls_close(h->tls);
  else if (h->fd >= 0)
    close(h->fd);
  free(h);
}

typedef enum { HA_CONNECTING, HA_TLS_HANDSHAKE, HA_SENDING, HA_READING_HEADERS } http_async_phase_t;

struct http_async {
  http_async_phase_t phase;
  short want_events;
  http_url_t url; /* current hop; redirects update this */
  char user_agent[128];
  int insecure;
  char extra_header[512];
  int have_extra_header;
  int redirects;
  struct http *h; /* reused (memset + refilled) across redirect hops */
  char reqbuf[2048];
  size_t req_len, req_sent;
  size_t hdr_got; /* bytes accumulated in h->hold while headers aren't complete yet */
};

static ssize_t raw_send_nb(struct http *h, const char *buf, size_t n) {
  ssize_t w;

  if (h->tls)
    return tls_write(h->tls, buf, n); /* already 0-on-transient */
  w = send(h->fd, buf, n, MSG_NOSIGNAL);
  if (w < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return 0;
    log_line("send: %s", strerror(errno));
    return -1;
  }
  return w;
}

static int async_build_request(http_async_t *a) {
  char hostport[300];
  char hdrline[sizeof a->extra_header + 4] = "";
  unsigned default_port = a->h->url.tls ? 443 : 80;
  int rl;

  if (a->have_extra_header)
    snprintf(hdrline, sizeof hdrline, "%s\r\n", a->extra_header);
  if (a->h->url.port == default_port)
    bufcpy(hostport, sizeof hostport, a->h->url.host);
  else
    snprintf(hostport, sizeof hostport, "%s:%u", a->h->url.host, a->h->url.port);
  rl = snprintf(a->reqbuf, sizeof a->reqbuf, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nIcy-MetaData: 1\r\n%sConnection: close\r\n\r\n",
                a->h->url.path, hostport, a->user_agent, hdrline);
  if (rl < 0 || rl >= (int)sizeof a->reqbuf) {
    log_line("http request too long");
    return -1;
  }
  a->req_len = (size_t)rl;
  a->req_sent = 0;
  return 0;
}

/* (re)starts the connect for a->url into a->h, fresh each hop (initial + every redirect) */
static int async_start_connect(http_async_t *a, net_err_reason_t *reason_out) {
  int fd = netconnect_tcp_start(a->url.host, a->url.port, reason_out);
  if (fd < 0)
    return -1;
  memset(a->h, 0, sizeof *a->h);
  a->h->fd = fd;
  a->h->url = a->url;
  a->phase = HA_CONNECTING;
  a->want_events = POLLOUT;
  a->hdr_got = 0;
  return 0;
}

http_async_t *http_async_start(const http_url_t *url_in, const char *user_agent, int insecure, const char *extra_header, net_err_reason_t *reason_out) {
  http_async_t *a = calloc(1, sizeof *a);
  if (!a)
    return NULL;
  a->h = calloc(1, sizeof *a->h);
  if (!a->h) {
    free(a);
    return NULL;
  }
  a->url = *url_in;
  bufcpy(a->user_agent, sizeof a->user_agent, user_agent ? user_agent : "dvbipitools");
  a->insecure = insecure;
  if (extra_header) {
    bufcpy(a->extra_header, sizeof a->extra_header, extra_header);
    a->have_extra_header = 1;
  }
  if (async_start_connect(a, reason_out) != 0) {
    free(a->h);
    free(a);
    return NULL;
  }
  return a;
}

int http_async_poll_fd(const http_async_t *a) { return a->h->fd; }

short http_async_poll_events(const http_async_t *a) { return a->want_events; }

static http_async_state_t async_after_headers(http_async_t *a, net_err_reason_t *reason_out) {
  struct http *h = a->h;

  if (h->status == 301 || h->status == 302 || h->status == 303 || h->status == 307 || h->status == 308) {
    const char *loc;
    http_url_t next;
    if (a->redirects >= HTTP_REDIRECT_MAX) {
      log_line("http: too many redirects");
      if (reason_out)
        *reason_out = NET_ERR_HTTP;
      return HTTP_ASYNC_ERROR;
    }
    loc = http_header(h, "location");
    next = h->url;
    if (!loc || resolve_location(&next, loc) != 0) {
      log_line("http: redirect without usable Location");
      if (reason_out)
        *reason_out = NET_ERR_FORMAT;
      return HTTP_ASYNC_ERROR;
    }
    if (h->tls) {
      tls_close(h->tls);
      h->tls = NULL;
    } else if (h->fd >= 0) {
      close(h->fd);
    }
    a->redirects++;
    a->url = next;
    if (async_start_connect(a, reason_out) != 0)
      return HTTP_ASYNC_ERROR;
    return HTTP_ASYNC_PENDING;
  }
  if (h->status == 304)
    return HTTP_ASYNC_DONE; /* not-modified: caller checks http_status(), body is empty */
  if (h->status < 200 || h->status >= 300) {
    log_line("http %d fetching %s%s", h->status, h->url.host, h->url.path);
    if (reason_out)
      *reason_out = NET_ERR_HTTP;
    return HTTP_ASYNC_ERROR;
  }
  return HTTP_ASYNC_DONE;
}

http_async_state_t http_async_step(http_async_t *a, net_err_reason_t *reason_out) {
  struct http *h = a->h;

  if (a->phase == HA_CONNECTING) {
    if (netconnect_tcp_finish(h->fd, reason_out) < 0) {
      log_line("connect %s:%u: %s", h->url.host, h->url.port, strerror(errno));
      return HTTP_ASYNC_ERROR;
    }
    if (h->url.tls) {
      h->tls = tls_connect_start(h->fd, h->url.host, a->insecure);
      if (!h->tls) {
        if (reason_out) /* already logged */
          *reason_out = NET_ERR_TLS;
        return HTTP_ASYNC_ERROR;
      }
      a->phase = HA_TLS_HANDSHAKE;
      a->want_events = POLLOUT; /* SSL_connect()'s first move is normally outbound */
      return HTTP_ASYNC_PENDING;
    }
    if (async_build_request(a) != 0) {
      if (reason_out)
        *reason_out = NET_ERR_FORMAT;
      return HTTP_ASYNC_ERROR;
    }
    a->phase = HA_SENDING;
    a->want_events = POLLOUT;
    return HTTP_ASYNC_PENDING;
  }

  if (a->phase == HA_TLS_HANDSHAKE) {
    tls_handshake_status_t st = tls_handshake_step(h->tls);
    if (st == TLS_HANDSHAKE_WANT_READ) {
      a->want_events = POLLIN;
      return HTTP_ASYNC_PENDING;
    }
    if (st == TLS_HANDSHAKE_WANT_WRITE) {
      a->want_events = POLLOUT;
      return HTTP_ASYNC_PENDING;
    }
    if (st == TLS_HANDSHAKE_ERROR) {
      if (reason_out)
        *reason_out = NET_ERR_TLS;
      return HTTP_ASYNC_ERROR;
    }
    if (async_build_request(a) != 0) {
      if (reason_out)
        *reason_out = NET_ERR_FORMAT;
      return HTTP_ASYNC_ERROR;
    }
    a->phase = HA_SENDING;
    a->want_events = POLLOUT;
    return HTTP_ASYNC_PENDING;
  }

  if (a->phase == HA_SENDING) {
    while (a->req_sent < a->req_len) {
      ssize_t n = raw_send_nb(h, a->reqbuf + a->req_sent, a->req_len - a->req_sent);
      if (n < 0) {
        if (reason_out)
          *reason_out = NET_ERR_READ;
        return HTTP_ASYNC_ERROR;
      }
      if (n == 0) {
        a->want_events = POLLOUT;
        return HTTP_ASYNC_PENDING;
      }
      a->req_sent += (size_t)n;
    }
    a->phase = HA_READING_HEADERS;
    a->want_events = POLLIN;
    return HTTP_ASYNC_PENDING;
  }

  { /* HA_READING_HEADERS */
    char *term;
    size_t termlen = 0;

    for (;;) {
      ssize_t n;
      if (a->hdr_got >= sizeof h->hold) {
        log_line("http: response headers too large");
        if (reason_out)
          *reason_out = NET_ERR_FORMAT;
        return HTTP_ASYNC_ERROR;
      }
      n = raw_recv(h, h->hold + a->hdr_got, sizeof h->hold - a->hdr_got, reason_out);
      if (n < 0) {
        log_line("http: connection closed while reading headers");
        return HTTP_ASYNC_ERROR;
      }
      if (n == 0) {
        a->want_events = POLLIN;
        return HTTP_ASYNC_PENDING;
      }
      a->hdr_got += (size_t)n;
      term = find_header_end((char *)h->hold, a->hdr_got, &termlen);
      if (term)
        break;
    }
    {
      size_t hdrlen = (size_t)(term - (char *)h->hold);
      size_t consumed = hdrlen + termlen;
      char block[sizeof h->hold];
      memcpy(block, h->hold, hdrlen);
      block[hdrlen] = '\0';
      parse_headers(h, block);
      h->hlen = a->hdr_got - consumed;
      memmove(h->hold, h->hold + consumed, h->hlen);
      h->hpos = 0;
    }
    if (setup_transfer_encoding(h, reason_out) != 0)
      return HTTP_ASYNC_ERROR;
    return async_after_headers(a, reason_out);
  }
}

http_t *http_async_take(http_async_t *a) {
  struct http *h = a->h;
  free(a);
  return h;
}

void http_async_free(http_async_t *a) {
  if (!a)
    return;
  if (a->h) {
    if (a->h->tls)
      tls_close(a->h->tls);
    else if (a->h->fd >= 0)
      close(a->h->fd);
    free(a->h);
  }
  free(a);
}
