/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_FCCRET_FCC_CLIENT_H
#define DVBIPITOOLS_LIB_FCCRET_FCC_CLIENT_H

#include <stddef.h>
#include <sys/types.h>

#include "lib/demux/rtcp.h"
#include "lib/demux/rtp.h"
#include "lib/net/multicast.h"

typedef struct {
  int family; /* FCC server unicast family */
  char addr[64]; /* FCC server unicast address */
  unsigned port;
  unsigned char rtx_pt; /* burst RTP payload type, matches dipifccret's -R */
  unsigned min_buffer_fill_ms; /* RAMS-R type 2, 0 = no requirement */
  unsigned max_buffer_fill_ms; /* RAMS-R type 3, 0 = no requirement */
} fcc_client_cfg_t;

typedef struct fcc_client fcc_client_t;

/* NULL on socket/connect failure */
fcc_client_t *fcc_client_open(const fcc_client_cfg_t *cfg);

/* burst-then-multicast TS payload. >0 len, 0 no data, -1 error */
ssize_t fcc_client_read(fcc_client_t *r, mcast_t *main, unsigned char *buf, size_t cap);

/* 1 once cutover (RAMS-T sent or burst rejected) has happened, 0 while still bursting */
int fcc_client_done(const fcc_client_t *r);

void fcc_client_close(fcc_client_t *r);

/* state machine, exposed for unit tests with synthetic packets */
void fcc_on_uni(fcc_client_t *r, const unsigned char *pkt, size_t len, double now);
void fcc_on_multicast(fcc_client_t *r, const rtp_hdr_t *hdr, const unsigned char *payload, size_t len, double now);

#endif
