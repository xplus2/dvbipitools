/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_HTTP3_STATELESS_H
#define DIPIXY_HTTP3_STATELESS_H

#ifdef HAVE_HTTP3

#include <ngtcp2/ngtcp2.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <time.h>

#define H3_SCID_LEN 16

static inline ngtcp2_tstamp h3_ts(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ngtcp2_tstamp)ts.tv_sec * 1000000000ULL + (ngtcp2_tstamp)ts.tv_nsec;
}

typedef enum {
  H3_RETRY_OFF = 0,
  H3_RETRY_AUTO,
  H3_RETRY_ALWAYS
} h3_retry_mode_t;

/* token state for h3conn_new(). retried: token from "Retry" */
typedef struct {
  const uint8_t *token;
  size_t tokenlen;
  ngtcp2_token_type type;
  int retried;
  /* retried: DCID of 1st "Initial" */
  ngtcp2_cid odcid;
} h3_admit_t;

/* 0 on success. call before workers start */
int h3_stateless_init(void);

void h3_stateless_set_retry(h3_retry_mode_t mode);
int h3_retry_needed(h3_retry_mode_t mode, int active, int max_conns);

ngtcp2_ssize h3_token_retry_make(uint8_t *out, const struct sockaddr *peer, socklen_t peerlen, uint32_t version, const ngtcp2_cid *retry_scid, const ngtcp2_cid *odcid, ngtcp2_tstamp ts);
int h3_token_retry_check(ngtcp2_cid *odcid, const uint8_t *tok, size_t toklen, const struct sockaddr *peer, socklen_t peerlen, uint32_t version, const ngtcp2_cid *dcid, ngtcp2_tstamp ts);
ngtcp2_ssize h3_token_new_make(uint8_t *out, const struct sockaddr *peer, socklen_t peerlen, ngtcp2_tstamp ts);
int h3_token_new_check(const uint8_t *tok, size_t toklen, const struct sockaddr *peer, socklen_t peerlen, ngtcp2_tstamp ts);

/* stateless reset token bound to cid, 0 = success */
int h3_reset_token(uint8_t *token, const ngtcp2_cid *cid);

/* NEW_TOKEN for clients after handshake */
void h3_stateless_offer_token(ngtcp2_conn *qconn, const struct sockaddr *peer, socklen_t peerlen);
int h3_stateless_admit(int udp_fd, const uint8_t *pkt, size_t pktlen, const struct sockaddr *peer, socklen_t peerlen, int active, int max_conns, ngtcp2_pkt_hd *hd, h3_admit_t *ai);

#endif /* HAVE_HTTP3 */

#endif /* DIPIXY_HTTP3_STATELESS_H */
