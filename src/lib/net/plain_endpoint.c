/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/uriparse.h"

#include "plain_endpoint.h"

static int plain_endpoint_parse_direct(const char *rest, plain_endpoint_t *s) {
  if (*rest == '@') rest++;
  return uriparse_mcast_addrport(rest, &s->family, s->group, sizeof s->group, &s->port);
}

int plain_endpoint_parse(const char *uri, plain_endpoint_t *s, int is_sink) {
  memset(s, 0, sizeof *s);
  if (strcmp(uri, "-") == 0) {
    s->kind = PLAIN_EP_FILE; /* file_path[0] == '\0': stdin (source) / stdout (sink) */
    return 0;
  }
  if (strncmp(uri, "rtp://", 6) == 0) {
    s->kind = PLAIN_EP_RTP;
    s->rtp_wrapped = 1;
    return plain_endpoint_parse_direct(uri + 6, s);
  }
  if (strncmp(uri, "udp://", 6) == 0) {
    s->kind = PLAIN_EP_UDP;
    s->rtp_wrapped = 0;
    return plain_endpoint_parse_direct(uri + 6, s);
  }
  if (strncmp(uri, "http://", 7) == 0 || strncmp(uri, "https://", 8) == 0) {
    if (is_sink) return -1; /* an HTTP TS source makes no sense as an output */
    s->kind = PLAIN_EP_HTTP;
    return http_url_parse(uri, &s->http);
  }
  if (strlen(uri) >= sizeof s->file_path) return -1;
  s->kind = PLAIN_EP_FILE;
  bufcpy(s->file_path, sizeof s->file_path, uri);
  return 0;
}

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
