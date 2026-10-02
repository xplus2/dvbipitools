/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/describe.h"
#include "lib/sys/ioutil.h"
#include "lib/net/plain_endpoint.h"

#include "priv.h"

/* count: prior calls for this -i/-o (caller's n_in/n_out). first call: decides is_srt.
   later calls: must match, or rejected. bonded srt:// peers: must agree on @ (listen) */
int srt_parse_endpoint_uri(const char *uri, endpoint_t *e, int is_sink, int *count) {
  int is_srt_uri = strncmp(uri, "srt://", 6) == 0;
  if (*count == 0) {
    e->is_srt = is_srt_uri;
    e->n_srt = 0;
  } else if (!e->is_srt || !is_srt_uri) {
    return -1;
  }

  if (e->is_srt) {
    const char *rest = uri + 6;
    int has_at = *rest == '@';
    int family;
    char host[64];
    unsigned port;

    if (has_at) rest++;
    if (e->n_srt >= SRTCOMMON_MAX_PEERS) return -1;
    if (argutil_addrport_parse(rest, &family, host, sizeof host, &port)) return -1;
    if (e->n_srt == 0)
      e->listen = has_at;
    else if (e->listen != has_at)
      return -1; /* mixed caller/listener within one bonded endpoint makes no sense */
    e->family[e->n_srt] = family;
    bufcpy(e->srt_host[e->n_srt], sizeof e->srt_host[0], host);
    e->srt_port[e->n_srt] = port;
    e->n_srt++;
  } else if (plain_endpoint_parse(uri, &e->nonsrt, is_sink)) {
    return -1;
  }
  (*count)++;
  return 0;
}

int srt_cfg_add_endpoint(config_t *cfg, int is_out, const char *uri) {
  return is_out ? srt_parse_endpoint_uri(uri, &cfg->out, 1, &cfg->n_out) : srt_parse_endpoint_uri(uri, &cfg->in, 0, &cfg->n_in);
}

int config_is_sender(const config_t *cfg) {
  return cfg->out.is_srt;
}

void endpoint_describe(const endpoint_t *e, char *buf, size_t n) {
  if (e->is_srt) {
    char first[96];
    describe_srt_uri(first, sizeof first, e->family[0], e->listen, e->srt_host[0], e->srt_port[0]);
    if (e->n_srt == 1) {
      bufcpy(buf, n, first);
    } else {
      sbuf_t b;
      sbuf_init(&b, buf, n);
      sbuf_add(&b, first);
      sbuf_add(&b, " +");
      sbuf_add_uint(&b, (unsigned)(e->n_srt - 1));
      sbuf_add(&b, " more");
    }
    return;
  }
  switch (e->nonsrt.kind) {
  case PLAIN_EP_RTP:
  case PLAIN_EP_UDP: {
    const char *scheme = (e->nonsrt.kind == PLAIN_EP_RTP) ? "rtp" : "udp";
    describe_mcast_uri(buf, n, scheme, e->nonsrt.family, e->nonsrt.group, e->nonsrt.port);
    break;
  }
  case PLAIN_EP_HTTP:
    describe_http_uri(buf, n, e->nonsrt.http.tls, e->nonsrt.http.host, e->nonsrt.http.port, e->nonsrt.http.path);
    break;
  case PLAIN_EP_FILE:
    bufcpy(buf, n, e->nonsrt.file_path[0] ? e->nonsrt.file_path : "- (stdin/stdout)");
    break;
  }
}
