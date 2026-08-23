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
  case SRC_RIST:
    return TSSRC_RIST;
  }
  return TSSRC_STDIN;
}

tvsrc_t *tvsrc_open(const config_t *cfg, const dipitvhead_input_t *input, net_err_reason_t *reason_out) {
  tssrc_cfg_t tc;
  tvsrc_t *s = calloc(1, sizeof *s);
  if (!s)
    return NULL;

  memset(&tc, 0, sizeof tc);
  tc.kind = tssrc_kind_of(input->input.kind);
  tc.family = input->input.family;
  tc.group = input->input.group;
  tc.port = input->input.port;
  tc.iface = input->iface_in;
  tc.http = input->input.http;
  tc.insecure_tls = cfg->insecure_tls;
  tc.user_agent = TOOL_NAME "/" TOOL_VERSION;
  tc.rist_uri = input->input.rist_uri;
  tc.rist_profile_main = input->rist_profile_main;

  s->t = tssrc_open(&tc, reason_out);
  if (!s->t) {
    free(s);
    return NULL;
  }
  return s;
}

ssize_t tvsrc_read(tvsrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) { return tssrc_read(s->t, buf, cap, reason_out); }

int tvsrc_fd(const tvsrc_t *s) { return tssrc_fd(s->t); }

void tvsrc_close(tvsrc_t *s) {
  if (!s)
    return;
  tssrc_close(s->t);
  free(s);
}

struct tvsrc_open {
  tssrc_open_t *o;
};

tvsrc_open_t *tvsrc_open_async_start(const config_t *cfg, const dipitvhead_input_t *input, net_err_reason_t *reason_out) {
  tssrc_cfg_t tc;
  tvsrc_open_t *o = calloc(1, sizeof *o);
  if (!o)
    return NULL;

  memset(&tc, 0, sizeof tc);
  tc.kind = tssrc_kind_of(input->input.kind);
  tc.family = input->input.family;
  tc.group = input->input.group;
  tc.port = input->input.port;
  tc.iface = input->iface_in;
  tc.http = input->input.http;
  tc.insecure_tls = cfg->insecure_tls;
  tc.user_agent = TOOL_NAME "/" TOOL_VERSION;
  tc.rist_uri = input->input.rist_uri;
  tc.rist_profile_main = input->rist_profile_main;

  o->o = tssrc_open_async_start(&tc, reason_out);
  if (!o->o) {
    free(o);
    return NULL;
  }
  return o;
}

int tvsrc_open_async_poll_fd(const tvsrc_open_t *o) { return tssrc_open_async_poll_fd(o->o); }
short tvsrc_open_async_poll_events(const tvsrc_open_t *o) { return tssrc_open_async_poll_events(o->o); }

tvsrc_open_state_t tvsrc_open_async_step(tvsrc_open_t *o, net_err_reason_t *reason_out) {
  switch (tssrc_open_async_step(o->o, reason_out)) {
  case TSSRC_OPEN_DONE:
    return TVSRC_OPEN_DONE;
  case TSSRC_OPEN_ERROR:
    return TVSRC_OPEN_ERROR;
  default:
    return TVSRC_OPEN_PENDING;
  }
}

tvsrc_t *tvsrc_open_async_take(tvsrc_open_t *o) {
  tvsrc_t *s = calloc(1, sizeof *s);
  if (!s) {
    tssrc_close(tssrc_open_async_take(o->o));
    free(o);
    return NULL;
  }
  s->t = tssrc_open_async_take(o->o);
  free(o);
  return s;
}

void tvsrc_open_async_free(tvsrc_open_t *o) {
  if (!o)
    return;
  tssrc_open_async_free(o->o);
  free(o);
}
