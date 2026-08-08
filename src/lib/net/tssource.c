/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../demux/rtp.h"

#include "httpclient.h"
#include "multicast.h"
#include "tssource.h"
#include "udpxy.h"

struct tssrc {
  tssrc_kind_t kind;
  mcast_t *m;
  http_t *h;
  udpxy_t *u;
};

tssrc_t *tssrc_open(const tssrc_cfg_t *cfg, net_err_reason_t *reason_out) {
  const char *ua = cfg->user_agent ? cfg->user_agent : "dvbipitools";
  tssrc_t *s = calloc(1, sizeof *s);
  if (!s)
    return NULL;
  s->kind = cfg->kind;
  switch (s->kind) {
  case TSSRC_RTP:
  case TSSRC_UDP:
    s->m = mcast_open(cfg->family, cfg->group, cfg->port, cfg->iface, 1000);
    if (!s->m) {
      free(s);
      if (reason_out)
        *reason_out = NET_ERR_CONNECT;
      return NULL;
    }
    return s;
  case TSSRC_HTTP:
    s->h = http_get(&cfg->http, ua, cfg->insecure_tls, NULL, reason_out);
    if (!s->h) {
      free(s);
      return NULL;
    }
    return s;
  case TSSRC_UDPXY:
    s->u = udpxy_open(cfg->udpxy_host, cfg->udpxy_port, cfg->udpxy_path, ua, reason_out);
    if (!s->u) {
      free(s);
      return NULL;
    }
    return s;
  case TSSRC_STDIN:
    return s;
  }
  free(s);
  return NULL;
}

ssize_t tssrc_read(tssrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  ssize_t n;
  size_t off;

  switch (s->kind) {
  case TSSRC_RTP:
  case TSSRC_UDP:
    n = mcast_recv(s->m, buf, cap, reason_out);
    if (n <= 0)
      return n;
    off = rtp_payload_offset(buf, (size_t)n);
    if (off) {
      /* offset+len contract instead of copy: needs a tssrc_read() API change
         across both tools, saves a sub-us memmove. not worth it. */
      memmove(buf, buf + off, (size_t)n - off);
      n -= (ssize_t)off;
    }
    return n;
  case TSSRC_HTTP:
    return http_read(s->h, buf, cap, reason_out);
  case TSSRC_UDPXY:
    return udpxy_read(s->u, buf, cap, reason_out);
  case TSSRC_STDIN:
    n = read(STDIN_FILENO, buf, cap);
    if (n == 0) {
      if (reason_out)
        *reason_out = NET_ERR_EOF;
      return -1;
    }
    if (n < 0) {
      if (errno == EINTR)
        return 0;
      if (reason_out)
        *reason_out = NET_ERR_READ;
      return -1;
    }
    return n;
  }
  return -1;
}

mcast_t *tssrc_mcast(tssrc_t *s) { return s->m; }

int tssrc_fd(const tssrc_t *s) {
  switch (s->kind) {
  case TSSRC_RTP:
  case TSSRC_UDP:
    return mcast_fd(s->m);
  case TSSRC_HTTP:
    return http_fd(s->h);
  case TSSRC_STDIN:
    return STDIN_FILENO;
  case TSSRC_UDPXY:
    return -1;
  }
  return -1;
}

void tssrc_close(tssrc_t *s) {
  if (!s)
    return;
  if (s->m)
    mcast_close(s->m);
  if (s->h)
    http_close(s->h);
  if (s->u)
    udpxy_close(s->u);
  free(s);
}

struct tssrc_open {
  http_async_t *ha; /* TSSRC_HTTP only, during PENDING */
  tssrc_t *result;  /* set once done, any kind */
  int done;
};

tssrc_open_t *tssrc_open_async_start(const tssrc_cfg_t *cfg, net_err_reason_t *reason_out) {
  tssrc_open_t *o = calloc(1, sizeof *o);
  if (!o)
    return NULL;
  if (cfg->kind == TSSRC_HTTP) {
    const char *ua = cfg->user_agent ? cfg->user_agent : "dvbipitools";
    o->ha = http_async_start(&cfg->http, ua, cfg->insecure_tls, NULL, reason_out);
    if (!o->ha) {
      free(o);
      return NULL;
    }
    return o;
  }
  o->result = tssrc_open(cfg, reason_out); /* RTP/UDP/STDIN/UDPXY: cheap, local-only work, done synchronously */
  if (!o->result) {
    free(o);
    return NULL;
  }
  o->done = 1;
  return o;
}

int tssrc_open_async_poll_fd(const tssrc_open_t *o) {
  if (o->ha)
    return http_async_poll_fd(o->ha);
  return -1;
}

short tssrc_open_async_poll_events(const tssrc_open_t *o) {
  if (o->ha)
    return http_async_poll_events(o->ha);
  return 0;
}

tssrc_open_state_t tssrc_open_async_step(tssrc_open_t *o, net_err_reason_t *reason_out) {
  http_async_state_t st;

  if (o->done)
    return TSSRC_OPEN_DONE;

  st = http_async_step(o->ha, reason_out);
  if (st == HTTP_ASYNC_PENDING)
    return TSSRC_OPEN_PENDING;
  if (st == HTTP_ASYNC_ERROR) {
    http_async_free(o->ha);
    o->ha = NULL;
    return TSSRC_OPEN_ERROR;
  }

  o->result = calloc(1, sizeof *o->result);
  if (!o->result) {
    http_close(http_async_take(o->ha));
    o->ha = NULL;
    if (reason_out)
      *reason_out = NET_ERR_OTHER;
    return TSSRC_OPEN_ERROR;
  }
  o->result->kind = TSSRC_HTTP;
  o->result->h = http_async_take(o->ha);
  o->ha = NULL;
  o->done = 1;
  return TSSRC_OPEN_DONE;
}

tssrc_t *tssrc_open_async_take(tssrc_open_t *o) {
  tssrc_t *r = o->result;
  free(o);
  return r;
}

void tssrc_open_async_free(tssrc_open_t *o) {
  if (!o)
    return;
  if (o->ha)
    http_async_free(o->ha);
  if (o->result)
    tssrc_close(o->result);
  free(o);
}
