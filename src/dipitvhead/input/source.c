/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/net/ts/source.h"

#include "../version.h"
#include "source.h"

#define TVSRC_OPEN_TIMEOUT_S 30.0

struct tvsrc {
  tssrc_t *t;
};

static tssrc_kind_t tssrc_kind_of(src_kind_t k) {
  switch (k) {
    case SRC_RTP:   return TSSRC_RTP;
    case SRC_UDP:   return TSSRC_UDP;
    case SRC_HTTP:  return TSSRC_HTTP;
    case SRC_STDIN: return TSSRC_STDIN;
    case SRC_RIST:  return TSSRC_RIST;
    case SRC_SRT:   return TSSRC_SRT;
  }
  return TSSRC_STDIN;
}

static void fill_cfg(tssrc_cfg_t *tc_out, const config_t *cfg, const dipitvhead_input_t *input) {
  memset(tc_out, 0, sizeof *tc_out);
  tc_out->kind = tssrc_kind_of(input->input.kind);
  tc_out->family = input->input.family;
  tc_out->group = input->input.group;
  tc_out->source = input->input.source;
  tc_out->port = input->input.port;
  tc_out->iface = input->iface_in;
  tc_out->http = input->input.http;
  tc_out->insecure_tls = cfg->insecure_tls;
  tc_out->user_agent = TOOL_NAME "/" TOOL_VERSION;
  tc_out->rist_uri = input->input.rist_uri;
  tc_out->rist_profile_main = input->rist_profile_main;
  tc_out->rist_key_size = input->rist_key_size_in;
  tc_out->rist_secret = input->rist_secret_in;
  tc_out->rist_cname = input->rist_cname_in;
  tc_out->rist_buffer_ms = input->rist_buffer_ms_in;
  tc_out->srt_host = input->input.srt_host;
  tc_out->srt_port = input->input.srt_port;
  tc_out->srt_listen = input->input.srt_listen;
  tc_out->srt_passphrase = input->srt_passphrase_in;
  tc_out->srt_pbkeylen = input->srt_pbkeylen_in;
  tc_out->srt_streamid = input->srt_streamid_in;
  tc_out->srt_packetfilter = input->srt_packetfilter_in;
  tc_out->srt_latency_ms = input->srt_latency_in_ms;
  tc_out->jitter_ms = input->jitter_ms;
  tc_out->al_fec_l = input->al_fec_l_in;
  tc_out->al_fec_d = input->al_fec_d_in;
  tc_out->al_fec_port = input->al_fec_port_in;
  tc_out->srt_verbose = cfg->verbose;
}

tvsrc_t *tvsrc_open(const config_t *cfg, const dipitvhead_input_t *input, net_err_reason_t *reason_out) {
  tssrc_cfg_t tc;
  tvsrc_t *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  fill_cfg(&tc, cfg, input);

  s->t = tssrc_open_blocking(&tc, TVSRC_OPEN_TIMEOUT_S, reason_out);
  if (!s->t) {
    free(s);
    return NULL;
  }
  return s;
}

ssize_t tvsrc_read(tvsrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) { return tssrc_read(s->t, buf, cap, reason_out); }

int tvsrc_wait(const tvsrc_t *s, int timeout_ms) { return tssrc_wait(s->t, timeout_ms); }

int tvsrc_fd(const tvsrc_t *s) { return tssrc_fd(s->t); }

short tvsrc_poll_events(const tvsrc_t *s) { return tssrc_poll_events(s->t); }

int tvsrc_timer_driven(const tvsrc_t *s) { return tssrc_timer_driven(s->t); }

int tvsrc_enable_rx_timestamps(tvsrc_t *s) { return tssrc_enable_rx_timestamps(s->t); }

uint64_t tvsrc_last_rx_ns(const tvsrc_t *s) { return tssrc_last_rx_ns(s->t); }

int64_t tvsrc_buffer_ms(const tvsrc_t *s) { return tssrc_buffer_ms(s->t); }

void tvsrc_close(tvsrc_t *s) {
  if (!s) return;
  tssrc_close(s->t);
  free(s);
}

struct tvsrc_open {
  tssrc_open_t *o;
};

tvsrc_open_t *tvsrc_open_async_start(const config_t *cfg, const dipitvhead_input_t *input, net_err_reason_t *reason_out) {
  tssrc_cfg_t tc;
  tvsrc_open_t *o = calloc(1, sizeof *o);
  if (!o) return NULL;

  fill_cfg(&tc, cfg, input);

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
    case TSSRC_OPEN_DONE:  return TVSRC_OPEN_DONE;
    case TSSRC_OPEN_ERROR: return TVSRC_OPEN_ERROR;
    default:               return TVSRC_OPEN_PENDING;
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
  if (!o) return;
  tssrc_open_async_free(o->o);
  free(o);
}
