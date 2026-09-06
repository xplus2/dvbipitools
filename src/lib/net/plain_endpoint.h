/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_PLAIN_ENDPOINT_H
#define DVBIPITOOLS_LIB_NET_PLAIN_ENDPOINT_H

#include "httpclient/httpclient.h"
#include "tssink.h"
#include "tssource.h"

typedef enum {
  PLAIN_EP_RTP,  /* multicast, RTP wrapped */
  PLAIN_EP_UDP,  /* multicast, plain ts */
  PLAIN_EP_HTTP, /* http:// or https://, http_url_t.tls tells which. -i (source side) only */
  PLAIN_EP_FILE  /* stdin/stdout ("-") or a local file path */
} plain_endpoint_kind_t;

typedef struct {
  plain_endpoint_kind_t kind;
  int rtp_wrapped; /* RTP payload. PLAIN_EP_RTP / PLAIN_EP_UDP only, protocol-inherent */
  int family; /* AF_INET or AF_INET6. PLAIN_EP_RTP/PLAIN_EP_UDP only */
  char group[64];
  unsigned port;
  unsigned al_fec_l;
  unsigned al_fec_d;
  unsigned al_fec_port;
  http_url_t http; /* PLAIN_EP_HTTP */
  char file_path[512]; /* "" = stdin (source) or stdout (sink) */
} plain_endpoint_t;

void plain_endpoint_to_tssrc_cfg(const plain_endpoint_t *s, const char *iface, const char *user_agent, int insecure_tls, tssrc_cfg_t *tc);
void plain_endpoint_to_tssink_cfg(const plain_endpoint_t *s, const char *iface, tssink_cfg_t *tk);

#endif
