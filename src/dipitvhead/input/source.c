/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/demux/rtp.h"
#include "lib/net/httpclient.h"
#include "lib/net/multicast.h"

#include "../version.h"
#include "source.h"

struct tvsrc {
  src_kind_t kind;
  mcast_t *m;
  http_t *h;
};

tvsrc_t *tvsrc_open(const config_t *cfg) {
  tvsrc_t *s = calloc(1, sizeof *s);
  if (!s)
    return NULL;
  s->kind = cfg->input.kind;
  switch (s->kind) {
    case SRC_RTP:
    case SRC_UDP:
      s->m = mcast_open(cfg->input.family, cfg->input.group, cfg->input.port, cfg->iface, 1000);
      if (!s->m) {
        free(s);
        return NULL;
      }
      return s;
    case SRC_HTTP:
      s->h = http_get(&cfg->input.http, TOOL_NAME "/" TOOL_VERSION, cfg->insecure_tls);
      if (!s->h) {
        free(s);
        return NULL;
      }
      return s;
    case SRC_STDIN:
      return s;
  }
  free(s);
  return NULL;
}

ssize_t tvsrc_read(tvsrc_t *s, unsigned char *buf, size_t cap) {
  ssize_t n;
  size_t off;

  switch (s->kind) {
    case SRC_RTP:
    case SRC_UDP:
      n = mcast_recv(s->m, buf, cap);
      if (n <= 0)
        return n;
      off = rtp_payload_offset(buf, (size_t)n);
      if (off) {
        memmove(buf, buf + off, (size_t)n - off);
        n -= (ssize_t)off;
      }
      return n;
    case SRC_HTTP:
      return http_read(s->h, buf, cap);
    case SRC_STDIN:
      n = read(STDIN_FILENO, buf, cap);
      if (n == 0)
        return -1; /* EOF */
      if (n < 0)
        return (errno == EINTR) ? 0 : -1;
      return n;
  }
  return -1;
}

void tvsrc_close(tvsrc_t *s) {
  if (!s)
    return;
  if (s->m)
    mcast_close(s->m);
  if (s->h)
    http_close(s->h);
  free(s);
}
