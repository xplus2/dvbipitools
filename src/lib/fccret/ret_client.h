/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_FCCRET_RET_CLIENT_H
#define DVBIPITOOLS_LIB_FCCRET_RET_CLIENT_H

#include <stddef.h>
#include <sys/types.h>

#include "lib/demux/rtp.h"
#include "lib/net/multicast.h"

typedef struct {
  int family; /* RET server, not source_family below */
  char addr[64]; /* RET server unicast addr, also MC repair session's SSM source filter */
  unsigned port;
  int mc_enabled;
  unsigned mc_port; /* 0 = reuse source_port, F.6.2.2 */
  unsigned char rtx_pt;
  unsigned wait_ms;

  int source_family; /* original channel */
  char source_group[64];
  unsigned source_port;
  const char *iface_in;
} ret_client_cfg_t;

typedef struct ret_client ret_client_t;

/* NULL on socket/join failure */
ret_client_t *ret_client_open(const ret_client_cfg_t *cfg);

/* seq-ordered TS payload, gaps NACKed and repaired within cfg->wait_ms. >0 len, 0 no data, -1 error */
ssize_t ret_client_read(ret_client_t *r, mcast_t *main, unsigned char *buf, size_t cap);

void ret_client_close(ret_client_t *r);

/* gap-tracking state machine, exposed for unit tests with synthetic headers/payloads;
   ret_client_t stays opaque, results still come back through ret_client_read()'s outq */
void on_original(ret_client_t *r, const rtp_hdr_t *hdr, const unsigned char *payload, size_t len, double now);
void on_repair(ret_client_t *r, const unsigned char *pkt, size_t len, double now);
void flush_ready(ret_client_t *r, double now);

#endif
