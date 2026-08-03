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

tssrc_t *tssrc_open(const tssrc_cfg_t *cfg) {
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
      return NULL;
    }
    return s;
  case TSSRC_HTTP:
    s->h = http_get(&cfg->http, ua, cfg->insecure_tls, NULL);
    if (!s->h) {
      free(s);
      return NULL;
    }
    return s;
  case TSSRC_UDPXY:
    s->u = udpxy_open(cfg->udpxy_host, cfg->udpxy_port, cfg->udpxy_path, ua);
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

ssize_t tssrc_read(tssrc_t *s, unsigned char *buf, size_t cap) {
  ssize_t n;
  size_t off;

  switch (s->kind) {
  case TSSRC_RTP:
  case TSSRC_UDP:
    n = mcast_recv(s->m, buf, cap);
    if (n <= 0)
      return n;
    off = rtp_payload_offset(buf, (size_t)n);
    if (off) {
      memmove(buf, buf + off, (size_t)n - off);
      n -= (ssize_t)off;
    }
    return n;
  case TSSRC_HTTP:
    return http_read(s->h, buf, cap);
  case TSSRC_UDPXY:
    return udpxy_read(s->u, buf, cap);
  case TSSRC_STDIN:
    n = read(STDIN_FILENO, buf, cap);
    if (n == 0)
      return -1; /* EOF */
    if (n < 0)
      return (errno == EINTR) ? 0 : -1;
    return n;
  }
  return -1;
}

mcast_t *tssrc_mcast(tssrc_t *s) { return s->m; }

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
