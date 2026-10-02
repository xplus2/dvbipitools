/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/describe.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/net/plain_endpoint.h"

#include "priv.h"

/* *count tracks with this flag: 0 = decides is_rist, further rist:// bond onto same endpoint, others (mixed, or a repeated non-RIST endpoint) rejected */
int rist_parse_endpoint_uri(const char *uri, endpoint_t *e, int is_sink, int *count) {
  int is_rist_uri = strncmp(uri, "rist://", 7) == 0;
  if (*count == 0) {
    e->is_rist = is_rist_uri;
    e->n_rist = 0;
  } else if (!e->is_rist || !is_rist_uri) {
    return -1;
  }

  if (e->is_rist) {
    int has_at = uri[7] == '@'; /* librist: '@' right after rist:// binds/listens, else it calls out */

#ifdef DIPIRIST_LIBRIST_IPV6_WARN
    if (uri[7 + has_at] == '[')
      log_line("warning: this librist build (<=0.2.20) has known IPv6 handling bugs, %s may crash it", uri);
#endif
    if (e->n_rist >= DIPIRIST_MAX_PEERS) return -1;
    if (strlen(uri) >= sizeof e->rist_uri[0]) return -1;
    /* -i rist:// listens for sender, -o rist:// calls out to receiver */
    if (is_sink == has_at) return -1;
    bufcpy(e->rist_uri[e->n_rist++], sizeof e->rist_uri[0], uri);
  } else if (plain_endpoint_parse(uri, &e->nonrist, is_sink)) {
    return -1;
  }
  (*count)++;
  return 0;
}

int rist_cfg_add_endpoint(config_t *cfg, int is_out, const char *uri) {
  return is_out ? rist_parse_endpoint_uri(uri, &cfg->out, 1, &cfg->n_out) : rist_parse_endpoint_uri(uri, &cfg->in, 0, &cfg->n_in);
}

int config_is_sender(const config_t *cfg) {
  return cfg->out.is_rist;
}

void endpoint_describe(const endpoint_t *e, char *buf, size_t n) {
  if (e->is_rist) {
    if (e->n_rist == 1) {
      bufcpy(buf, n, e->rist_uri[0]);
    } else {
      sbuf_t b;
      sbuf_init(&b, buf, n);
      sbuf_add(&b, e->rist_uri[0]);
      sbuf_add(&b, " +");
      sbuf_add_uint(&b, (unsigned)(e->n_rist - 1));
      sbuf_add(&b, " more");
    }
    return;
  }
  switch (e->nonrist.kind) {
  case PLAIN_EP_RTP:
  case PLAIN_EP_UDP: {
    const char *scheme = (e->nonrist.kind == PLAIN_EP_RTP) ? "rtp" : "udp";
    describe_mcast_uri(buf, n, scheme, e->nonrist.family, e->nonrist.group, e->nonrist.port);
    break;
  }
  case PLAIN_EP_HTTP:
    describe_http_uri(buf, n, e->nonrist.http.tls, e->nonrist.http.host, e->nonrist.http.port, e->nonrist.http.path);
    break;
  case PLAIN_EP_FILE:
    bufcpy(buf, n, e->nonrist.file_path[0] ? e->nonrist.file_path : "- (stdin/stdout)");
    break;
  }
}
