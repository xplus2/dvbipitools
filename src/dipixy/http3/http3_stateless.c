/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* stateless QUIC: Retry, version negotiation, reset, invalid-token close, tokens */

#ifdef HAVE_HTTP3

#include "http3_stateless.h"

#include <ngtcp2/ngtcp2_crypto.h>
#include <openssl/rand.h>
#include <string.h>
#include <sys/socket.h>

#define H3_RETRY_TOKEN_NS (10ULL * 1000000000ULL)
#define H3_NEW_TOKEN_NS (3600ULL * 1000000000ULL)
#define H3_STATELESS_PER_SEC 1000u
#define H3_RESET_TARGET_LEN 43

static uint8_t g_secret[32];
static h3_retry_mode_t g_mode = H3_RETRY_AUTO;

int h3_stateless_init(void) {
  return RAND_bytes(g_secret, sizeof g_secret) == 1 ? 0 : -1;
}

void h3_stateless_set_retry(h3_retry_mode_t mode) {
  g_mode = mode;
}

int h3_retry_needed(h3_retry_mode_t mode, int active, int max_conns) {
  if (mode == H3_RETRY_ALWAYS) return 1;
  if (mode == H3_RETRY_AUTO) return active >= max_conns / 2;
  return 0;
}

ngtcp2_ssize h3_token_retry_make(uint8_t *out, const struct sockaddr *peer, socklen_t peerlen, uint32_t version, const ngtcp2_cid *retry_scid, const ngtcp2_cid *odcid, ngtcp2_tstamp ts) {
  return ngtcp2_crypto_generate_retry_token2(out, g_secret, sizeof g_secret, version, peer, peerlen, retry_scid, odcid, ts);
}

int h3_token_retry_check(ngtcp2_cid *odcid, const uint8_t *tok, size_t toklen, const struct sockaddr *peer, socklen_t peerlen, uint32_t version, const ngtcp2_cid *dcid, ngtcp2_tstamp ts) {
  return ngtcp2_crypto_verify_retry_token2(odcid, tok, toklen, g_secret, sizeof g_secret, version, peer, peerlen, dcid, H3_RETRY_TOKEN_NS, ts);
}

ngtcp2_ssize h3_token_new_make(uint8_t *out, const struct sockaddr *peer, socklen_t peerlen, ngtcp2_tstamp ts) {
  return ngtcp2_crypto_generate_regular_token2(out, g_secret, sizeof g_secret, peer, peerlen, NULL, 0, ts);
}

int h3_token_new_check(const uint8_t *tok, size_t toklen, const struct sockaddr *peer, socklen_t peerlen, ngtcp2_tstamp ts) {
  return ngtcp2_crypto_verify_regular_token2(NULL, 0, tok, toklen, g_secret, sizeof g_secret, peer, peerlen, H3_NEW_TOKEN_NS, ts) < 0 ? -1 : 0;
}

int h3_reset_token(uint8_t *token, const ngtcp2_cid *cid) {
  return ngtcp2_crypto_generate_stateless_reset_token(token, g_secret, sizeof g_secret, cid);
}

void h3_stateless_offer_token(ngtcp2_conn *qconn, const struct sockaddr *peer, socklen_t peerlen) {
  uint8_t tok[NGTCP2_CRYPTO_MAX_REGULAR_TOKENLEN];
  if (g_mode == H3_RETRY_OFF) return;
  ngtcp2_ssize n = h3_token_new_make(tok, peer, peerlen, h3_ts());
  if (n > 0) ngtcp2_conn_submit_new_token(qconn, tok, (size_t)n);
}

static int budget_ok(void) {
  static _Thread_local uint64_t win_start;
  static _Thread_local unsigned sent;
  uint64_t now = h3_ts();
  if (now - win_start >= 1000000000ULL) {
    win_start = now;
    sent = 0;
  }
  return sent++ < H3_STATELESS_PER_SEC;
}

static void send_pkt(int fd, const uint8_t *buf, size_t len, const struct sockaddr *peer, socklen_t peerlen) {
  (void)sendto(fd, buf, len, 0, peer, peerlen);
}

static void send_version_neg(int fd, const ngtcp2_version_cid *vc, const struct sockaddr *peer, socklen_t peerlen) {
  static const uint32_t versions[] = {NGTCP2_PROTO_VER_V1};
  uint8_t buf[NGTCP2_MAX_UDP_PAYLOAD_SIZE];
  uint8_t rnd;
  if (!budget_ok() || RAND_bytes(&rnd, 1) != 1) return;
  ngtcp2_ssize n = ngtcp2_pkt_write_version_negotiation(buf, sizeof buf, rnd, vc->scid, vc->scidlen, vc->dcid, vc->dcidlen, versions, sizeof versions / sizeof versions[0]);
  if (n > 0) send_pkt(fd, buf, (size_t)n, peer, peerlen);
}

/* reply < trigger, else reset loops */
static void send_reset(int fd, size_t pktlen, const ngtcp2_version_cid *vc, const struct sockaddr *peer, socklen_t peerlen) {
  uint8_t rnd[H3_RESET_TARGET_LEN];
  uint8_t buf[H3_RESET_TARGET_LEN];
  ngtcp2_stateless_reset_token token = {0};
  ngtcp2_cid cid;
  size_t total = pktlen > H3_RESET_TARGET_LEN ? H3_RESET_TARGET_LEN : pktlen - 1;
  size_t randlen;
  if (total < NGTCP2_STATELESS_RESET_TOKENLEN + NGTCP2_MIN_STATELESS_RESET_RANDLEN) return;
  randlen = total - NGTCP2_STATELESS_RESET_TOKENLEN;
  if (!budget_ok() || vc->dcidlen > NGTCP2_MAX_CIDLEN || RAND_bytes(rnd, (int)randlen) != 1) return;
  ngtcp2_cid_init(&cid, vc->dcid, vc->dcidlen);
  if (h3_reset_token(token.data, &cid) != 0) return;
  ngtcp2_ssize n = ngtcp2_pkt_write_stateless_reset2(buf, sizeof buf, &token, rnd, randlen);
  if (n > 0 && (size_t)n < pktlen) send_pkt(fd, buf, (size_t)n, peer, peerlen);
}

static void send_retry(int fd, const ngtcp2_pkt_hd *hd, const struct sockaddr *peer, socklen_t peerlen, ngtcp2_tstamp ts) {
  uint8_t tok[NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN2];
  uint8_t buf[NGTCP2_MAX_UDP_PAYLOAD_SIZE];
  ngtcp2_cid scid;
  if (!budget_ok()) return;
  scid.datalen = H3_SCID_LEN;
  if (RAND_bytes(scid.data, H3_SCID_LEN) != 1) return;
  ngtcp2_ssize toklen = h3_token_retry_make(tok, peer, peerlen, hd->version, &scid, &hd->dcid, ts);
  if (toklen < 0) return;
  ngtcp2_ssize n = ngtcp2_crypto_write_retry(buf, sizeof buf, hd->version, &hd->scid, &scid, &hd->dcid, tok, (size_t)toklen);
  if (n > 0) send_pkt(fd, buf, (size_t)n, peer, peerlen);
}

static void send_invalid_token(int fd, const ngtcp2_pkt_hd *hd, const struct sockaddr *peer, socklen_t peerlen) {
  uint8_t buf[NGTCP2_MAX_UDP_PAYLOAD_SIZE];
  if (!budget_ok()) return;
  ngtcp2_ssize n = ngtcp2_crypto_write_connection_close(buf, sizeof buf, hd->version, &hd->scid, &hd->dcid, NGTCP2_INVALID_TOKEN, NULL, 0);
  if (n > 0) send_pkt(fd, buf, (size_t)n, peer, peerlen);
}

int h3_stateless_admit(int udp_fd, const uint8_t *pkt, size_t pktlen, const struct sockaddr *peer, socklen_t peerlen, int active, int max_conns, ngtcp2_pkt_hd *hd, h3_admit_t *ai) {
  ngtcp2_version_cid vc;
  ngtcp2_tstamp ts;
  int rv = ngtcp2_pkt_decode_version_cid(&vc, pkt, pktlen, H3_SCID_LEN);
  if (rv == NGTCP2_ERR_VERSION_NEGOTIATION) {
    send_version_neg(udp_fd, &vc, peer, peerlen);
    return 0;
  }
  if (rv != 0) return 0;
  if ((pkt[0] & 0x80) == 0) {
    send_reset(udp_fd, pktlen, &vc, peer, peerlen);
    return 0;
  }
  if (ngtcp2_accept(hd, pkt, pktlen) != 0) return 0;

  memset(ai, 0, sizeof *ai);
  ai->type = NGTCP2_TOKEN_TYPE_UNKNOWN;
  ts = h3_ts();
  if (hd->tokenlen > 0) {
    if (hd->token[0] == NGTCP2_CRYPTO_TOKEN_MAGIC_RETRY2) {
      if (h3_token_retry_check(&ai->odcid, hd->token, hd->tokenlen, peer, peerlen, hd->version, &hd->dcid, ts) != 0) {
        send_invalid_token(udp_fd, hd, peer, peerlen);
        return 0;
      }
      ai->token = hd->token;
      ai->tokenlen = hd->tokenlen;
      ai->type = NGTCP2_TOKEN_TYPE_RETRY;
      ai->retried = 1;
      return 1;
    }
    if (hd->token[0] == NGTCP2_CRYPTO_TOKEN_MAGIC_REGULAR && h3_token_new_check(hd->token, hd->tokenlen, peer, peerlen, ts) == 0) {
      ai->token = hd->token;
      ai->tokenlen = hd->tokenlen;
      ai->type = NGTCP2_TOKEN_TYPE_NEW_TOKEN;
      return 1;
    }
  }
  if (h3_retry_needed(g_mode, active, max_conns)) {
    send_retry(udp_fd, hd, peer, peerlen, ts);
    return 0;
  }
  return 1;
}

#endif /* HAVE_HTTP3 */
