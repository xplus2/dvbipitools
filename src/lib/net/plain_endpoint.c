/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "plain_endpoint.h"

void plain_endpoint_to_tssrc_cfg(const plain_endpoint_t *s, const char *iface, const char *user_agent, int insecure_tls, tssrc_cfg_t *tc) {
  memset(tc, 0, sizeof *tc);
  tc->user_agent = user_agent;
  switch (s->kind) {
  case PLAIN_EP_HTTP:
    tc->kind = TSSRC_HTTP;
    tc->http = s->http;
    tc->insecure_tls = insecure_tls;
    break;
  case PLAIN_EP_FILE:
    if (s->file_path[0]) {
      tc->kind = TSSRC_FILE;
      tc->file_path = s->file_path;
    } else {
      tc->kind = TSSRC_STDIN;
    }
    break;
  case PLAIN_EP_RTP:
  case PLAIN_EP_UDP:
    tc->kind = (s->kind == PLAIN_EP_RTP) ? TSSRC_RTP : TSSRC_UDP;
    tc->family = s->family;
    tc->group = s->group;
    tc->port = s->port;
    tc->iface = iface;
    tc->al_fec_l = s->al_fec_l;
    tc->al_fec_d = s->al_fec_d;
    tc->al_fec_port = s->al_fec_port;
    break;
  }
}

void plain_endpoint_to_tssink_cfg(const plain_endpoint_t *s, const char *iface, tssink_cfg_t *tk) {
  memset(tk, 0, sizeof *tk);
  if (s->kind == PLAIN_EP_FILE) {
    if (s->file_path[0]) {
      tk->kind = TSSINK_FILE;
      tk->file_path = s->file_path;
    } else {
      tk->kind = TSSINK_STDOUT;
    }
  } else {
    tk->kind = (s->kind == PLAIN_EP_RTP) ? TSSINK_RTP : TSSINK_UDP;
    tk->family = s->family;
    tk->group = s->group;
    tk->port = s->port;
    tk->iface = iface;
    tk->al_fec_l = s->al_fec_l;
    tk->al_fec_d = s->al_fec_d;
    tk->al_fec_port = s->al_fec_port;
  }
}
