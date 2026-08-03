/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/net/tssource.h"

#include "../version.h"
#include "source.h"

struct tvsrc {
  tssrc_t *t;
};

static tssrc_kind_t tssrc_kind_of(src_kind_t k) {
  switch (k) {
  case SRC_RTP:
    return TSSRC_RTP;
  case SRC_UDP:
    return TSSRC_UDP;
  case SRC_HTTP:
    return TSSRC_HTTP;
  case SRC_STDIN:
    return TSSRC_STDIN;
  }
  return TSSRC_STDIN;
}

tvsrc_t *tvsrc_open(const config_t *cfg) {
  tssrc_cfg_t tc;
  tvsrc_t *s = calloc(1, sizeof *s);
  if (!s)
    return NULL;

  memset(&tc, 0, sizeof tc);
  tc.kind = tssrc_kind_of(cfg->input.kind);
  tc.family = cfg->input.family;
  tc.group = cfg->input.group;
  tc.port = cfg->input.port;
  tc.iface = cfg->iface_in;
  tc.http = cfg->input.http;
  tc.insecure_tls = cfg->insecure_tls;
  tc.user_agent = TOOL_NAME "/" TOOL_VERSION;

  s->t = tssrc_open(&tc);
  if (!s->t) {
    free(s);
    return NULL;
  }
  return s;
}

ssize_t tvsrc_read(tvsrc_t *s, unsigned char *buf, size_t cap) { return tssrc_read(s->t, buf, cap); }

void tvsrc_close(tvsrc_t *s) {
  if (!s)
    return;
  tssrc_close(s->t);
  free(s);
}
