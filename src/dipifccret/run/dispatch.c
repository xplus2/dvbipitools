/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/demux/rtcp.h"
#include "lib/helper/log.h"
#include "lib/mux/rtcp_build.h"

#include "../fcc/burst.h"
#include "../version.h"
#include "run.h"

/* 0 on success, else errno */
static int send_unicast(int fd, const struct sockaddr *to, socklen_t tolen, const unsigned char *pkt, size_t len, int dscp) {
  if (to->sa_family == AF_INET6)
    setsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &dscp, sizeof dscp);
  else
    setsockopt(fd, IPPROTO_IP, IP_TOS, &dscp, sizeof dscp);
  if (sendto(fd, pkt, len, 0, to, tolen) < 0) {
    int err = errno;
    log_line(TOOL_NAME ": unicast sendto: %s", strerror(err));
    return err;
  }
  return 0;
}

void ret_send_mc_impl(const channel_t *c, const unsigned char *pkt, size_t len, int dscp, void *user) {
  ret_send_ctx_t *ctx = (ret_send_ctx_t *)user;
  mcast_t *m;
  if (!ctx->mt)
    return;
  m = mcsend_get(ctx->mt, c);
  if (!m)
    return; /* socket not provisioned yet, a NACK can race ahead of capture's first packet */
  mcast_set_tos(m, dscp);
  mcast_send(m, pkt, len);
}

void ret_send_unicast_impl(int fd, const struct sockaddr *to, socklen_t tolen, const unsigned char *pkt, size_t len, int dscp, void *user) {
  (void)user;
  send_unicast(fd, to, tolen, pkt, len, dscp);
}

void burst_send_cb(const unsigned char *pkt, size_t len, int dscp, void *user) {
  unicast_dest_t *dst = (unicast_dest_t *)user;
  int err = send_unicast(dst->fd, dst->to, dst->tolen, pkt, len, dscp);
  if (err == EAGAIN || err == EWOULDBLOCK || err == ENOBUFS)
    dst->congestion = 1;
}

void send_rams_i_msn(const unicast_dest_t *dst, uint32_t sender_ssrc, uint32_t media_ssrc, uint8_t msn, uint16_t response, const rtcp_rams_i_tlvs_t *tlvs) {
  unsigned char pkt[128];
  size_t n = rtcp_build_rams_i(sender_ssrc, media_ssrc, msn, response, tlvs, pkt, sizeof pkt);
  if (n > 0)
    send_unicast(dst->fd, dst->to, dst->tolen, pkt, n, RET_DSCP_RTCP);
}

void send_rams_i(const unicast_dest_t *dst, uint32_t sender_ssrc, uint32_t media_ssrc, uint16_t response, const rtcp_rams_i_tlvs_t *tlvs) {
  send_rams_i_msn(dst, sender_ssrc, media_ssrc, 0, response, tlvs);
}

#define CHANNEL_REAP_STEP_SLOTS 8 /* slots checked per packet: bounds cost vs one O(max_channels) sweep per interval */

/* fed by capture.c per whitelisted RTP-carried-TS packet, single demux feeds both RET ring & FCC cache */
void capture_cb(int family, const void *addr, size_t addr_len, unsigned port, unsigned char dscp, uint32_t ssrc, uint16_t seq, uint32_t timestamp, const unsigned char *payload, size_t payload_len, void *user) {
  dispatch_ctx_t *ctx = (dispatch_ctx_t *)user;
  channel_t *c;

  if (ctx->idle_timeout_s)
    channel_table_reap_step(ctx->channels, (time_t)ctx->idle_timeout_s, CHANNEL_REAP_STEP_SLOTS);
  if (ctx->ret && ctx->ret_client_idle_timeout_s)
    ret_ctx_reap_step(ctx->ret, (time_t)ctx->ret_client_idle_timeout_s, CHANNEL_REAP_STEP_SLOTS);

  c = channel_lookup(ctx->channels, family, addr, addr_len, port);
  if (!c)
    return; /* max-channels cap, already logged by channel_lookup */
  channel_store(ctx->channels, c, ssrc, seq, timestamp, dscp, payload, payload_len);
  if (ctx->mt)
    mcsend_ensure(ctx->mt, c, ctx->ff_port); /* cheap no-op if c already has a socket */
  if (ctx->rsi_mt)
    mcsend_ensure(ctx->rsi_mt, c, 0); /* always own port per channel, RSI address, not -F's */
}

typedef struct {
  dispatch_ctx_t *ctx;
  int fd;
  const struct sockaddr *from;
  socklen_t fromlen;
  int has_sdes; /* first-pass SDES scan of this same datagram, F.5.3 SRBT 8 primary check */
  uint32_t sdes_ssrc;
  char sdes_cname[RTCP_CNAME_MAX];
  size_t sdes_cname_len;
  channel_t *resolved; /* pre-resolved via dedicated port, NULL on shared -l (SSRC lookup instead) */
} listen_req_ctx_t;

/* cname for ssrc, only if a first-pass SDES scan of this datagram found one for it */
static void hned_cname_for(const listen_req_ctx_t *rc, uint32_t ssrc, const char **cname, size_t *cname_len) {
  if (rc->has_sdes && rc->sdes_ssrc == ssrc) {
    *cname = rc->sdes_cname;
    *cname_len = rc->sdes_cname_len;
  } else {
    *cname = NULL;
    *cname_len = 0;
  }
}

static void nack_cb(const rtcp_nack_t *nack, void *user) {
  listen_req_ctx_t *rc = (listen_req_ctx_t *)user;
  if (nack->truncated) {
    if (!atomic_load_explicit(&rc->ctx->nack_truncated_logged, memory_order_relaxed)) {
      log_line(TOOL_NAME ": NACK has more than %d FCI entries, dropping the rest", RTCP_NACK_MAX_ENTRIES);
      atomic_store_explicit(&rc->ctx->nack_truncated_logged, 1, memory_order_relaxed);
    }
  } else {
    atomic_store_explicit(&rc->ctx->nack_truncated_logged, 0, memory_order_relaxed);
  }
  if (rc->ctx->rsi_active) {
    channel_t *c = rc->resolved ? rc->resolved : channel_find_by_ssrc(rc->ctx->channels, nack->media_ssrc);
    if (c) {
      const char *cname;
      size_t cname_len;
      hned_cname_for(rc, nack->sender_ssrc, &cname, &cname_len);
      channel_hned_seen(c, nack->sender_ssrc, rc->from, rc->fromlen, cname, cname_len);
    }
  }
  ret_handle_nack(rc->ctx->ret, nack, rc->fd, rc->from, rc->fromlen);

  if (rc->ctx->bursts && rc->ctx->congestion_nack_threshold > 0) {
    burst_table_nack_result_t result;

    if (burst_table_note_nack(rc->ctx->bursts, rc->from, rc->fromlen, rc->ctx->congestion_nack_threshold, &result) &&
        result.action == BURST_TABLE_NACK_TERMINATED) {
      unicast_dest_t dst;
      dst.fd = rc->fd;
      dst.to = rc->from;
      dst.tolen = rc->fromlen;
      send_rams_i(&dst, 0, 0, (uint16_t)BURST_CONGESTION, NULL);
    } else if (result.action == BURST_TABLE_NACK_ADAPTED) {
      unicast_dest_t dst;
      rtcp_rams_i_tlvs_t tlvs;

      dst.fd = rc->fd;
      dst.to = rc->from;
      dst.tolen = rc->fromlen;
      memset(&tlvs, 0, sizeof tlvs);
      tlvs.has_max_transmit_bitrate = 1;
      tlvs.max_transmit_bitrate_bps = (uint64_t)result.new_bps;
      send_rams_i_msn(&dst, 0, 0, result.msn, (uint16_t)BURST_UPDATE, &tlvs);
    }
  }
}

static int addr_in_ranges(const struct sockaddr *sa, const cidr_t *ranges, size_t count) {
  if (sa->sa_family == AF_INET)
    return in_ranges(AF_INET, &((const struct sockaddr_in *)sa)->sin_addr, ranges, count);
  if (sa->sa_family == AF_INET6)
    return in_ranges(AF_INET6, &((const struct sockaddr_in6 *)sa)->sin6_addr, ranges, count);
  return 0;
}

static void rams_r_cb(const rtcp_rams_r_t *req, void *user) {
  listen_req_ctx_t *rc = (listen_req_ctx_t *)user;
  unicast_dest_t dst;
  channel_t *c;
  burst_response_t resp;
  rtcp_rams_i_tlvs_t tlvs;

  dst.fd = rc->fd;
  dst.to = rc->from;
  dst.tolen = rc->fromlen;

  if (!rc->ctx->bursts) {
    send_rams_i(&dst, req->media_ssrc, req->media_ssrc, (uint16_t)BURST_NOT_SUPPORTED, NULL);
    return;
  }
  if (rc->ctx->fcc_client_range_count > 0 && !addr_in_ranges(rc->from, rc->ctx->fcc_client_ranges, rc->ctx->fcc_client_range_count)) {
    send_rams_i(&dst, req->media_ssrc, req->media_ssrc, (uint16_t)BURST_NOT_ELIGIBLE, NULL);
    return;
  }

  if (rc->resolved)
    c = rc->resolved;
  else if (req->ignore_media_ssrc)
    c = NULL;
  else
    c = channel_find_by_ssrc(rc->ctx->channels, req->media_ssrc);
  if (c && rc->ctx->fcc_range_count > 0 && !in_ranges(c->family, c->addr, rc->ctx->fcc_ranges, rc->ctx->fcc_range_count)) {
    send_rams_i(&dst, req->media_ssrc, req->media_ssrc, (uint16_t)BURST_NOT_ENABLED, NULL);
    return;
  }
  if (c && rc->ctx->rsi_active) {
    const char *cname;
    size_t cname_len;
    hned_cname_for(rc, req->sender_ssrc, &cname, &cname_len);
    channel_hned_seen(c, req->sender_ssrc, rc->from, rc->fromlen, cname, cname_len);
  }
  resp = burst_decide(c, req, rc->ctx->max_buffer_fill_bound_ms);
  memset(&tlvs, 0, sizeof tlvs);
  if (resp != BURST_ACCEPT) {
    send_rams_i(&dst, req->media_ssrc, req->media_ssrc, (uint16_t)resp, NULL);
    return;
  }

  {
    burst_t *b = burst_new(c, rc->ctx->burst_multiplier, req->has_max_bitrate ? (double)req->max_bitrate_bps : 0.0, rc->ctx->rtx_pt);
    rap_cache_meta_t first;
    uint8_t msn;
    uint16_t response;
    int start_result;

    if (!b) {
      send_rams_i(&dst, req->media_ssrc, req->media_ssrc, (uint16_t)BURST_INTERNAL_ERROR, NULL);
      return;
    }

    start_result = burst_table_start(rc->ctx->bursts, rc->from, rc->fromlen, rc->fd, b, &msn);
    if (start_result < 0) {
      burst_free(b);
      send_rams_i(&dst, req->media_ssrc, req->media_ssrc, (uint16_t)BURST_TABLE_FULL, NULL);
      return;
    }
    response = start_result ? (uint16_t)BURST_UPDATE : (uint16_t)BURST_ACCEPT;

    if (channel_cache_peek_meta(c, 0, &first)) {
      tlvs.has_first_packet_seqnum = 1;
      tlvs.first_packet_seqnum = first.seq;
    }
    tlvs.has_earliest_join_time = 1;
    tlvs.earliest_join_time_ms = 0; /* join immediately, RFC 6285 7.3 TLV 33 zero value */
    tlvs.has_burst_duration = 1;
    tlvs.burst_duration_ms = rc->ctx->duration_cap_ms;
    tlvs.has_max_transmit_bitrate = 1;
    tlvs.max_transmit_bitrate_bps = (uint64_t)atomic_load_explicit(&b->target_bps, memory_order_relaxed);

    send_rams_i_msn(&dst, req->media_ssrc, req->media_ssrc, msn, response, &tlvs);
  }
}

static void rams_t_cb(const rtcp_rams_t_t *term, void *user) {
  listen_req_ctx_t *rc = (listen_req_ctx_t *)user;
  if (rc->ctx->rsi_active) {
    channel_t *c = rc->resolved ? rc->resolved : channel_find_by_ssrc(rc->ctx->channels, term->media_ssrc);
    if (c) {
      const char *cname;
      size_t cname_len;
      hned_cname_for(rc, term->sender_ssrc, &cname, &cname_len);
      channel_hned_seen(c, term->sender_ssrc, rc->from, rc->fromlen, cname, cname_len);
    }
  }
  burst_table_terminate(rc->ctx->bursts, rc->from, rc->fromlen, term->has_first_mc_seqnum, term->first_mc_seqnum);
}

static void sdes_cb(const rtcp_sdes_t *sdes, void *user) {
  listen_req_ctx_t *rc = (listen_req_ctx_t *)user;
  if (rc->has_sdes)
    return; /* one datagram normally reports one participant, first chunk wins */
  rc->has_sdes = 1;
  rc->sdes_ssrc = sdes->ssrc;
  memcpy(rc->sdes_cname, sdes->cname, sdes->cname_len);
  rc->sdes_cname_len = sdes->cname_len;
}

/* fires for a recognized-but-corrupt RAMS-T, RAMS-T itself never gets a normal response */
static void malformed_cb(unsigned sfmt, uint32_t sender_ssrc, uint32_t media_ssrc, void *user) {
  const listen_req_ctx_t *rc = (const listen_req_ctx_t *)user;
  unicast_dest_t dst;

  (void)sender_ssrc;
  if (sfmt != RTCP_SFMT_RAMS_T)
    return;
  dst.fd = rc->fd;
  dst.to = rc->from;
  dst.tolen = rc->fromlen;
  send_rams_i(&dst, media_ssrc, media_ssrc, (uint16_t)BURST_TERM_MALFORMED, NULL);
}

/* two rtcp_parse passes per datagram. pass1: collect SDES CNAME, compound order not
   guaranteed. pass2: dispatch NACK/RAMS-R/RAMS-T, cname already known */
static void dispatch_datagram(dispatch_ctx_t *ctx, channel_t *resolved, const unsigned char *pkt, size_t len, int fd, const struct sockaddr *from, socklen_t fromlen) {
  listen_req_ctx_t rc;
  rc.ctx = ctx;
  rc.fd = fd;
  rc.from = from;
  rc.fromlen = fromlen;
  rc.has_sdes = 0;
  rc.resolved = resolved;
  if (ctx->rsi_active)
    rtcp_parse(pkt, len, NULL, NULL, NULL, NULL, sdes_cb, NULL, &rc);
  rtcp_parse(pkt, len, ctx->ret ? &nack_cb : NULL, rams_r_cb, NULL, ctx->bursts ? &rams_t_cb : NULL, NULL, ctx->bursts ? &malformed_cb : NULL, &rc);
}

void listen_cb(const unsigned char *pkt, size_t len, int fd, const struct sockaddr *from, socklen_t fromlen, void *user) {
  dispatch_datagram((dispatch_ctx_t *)user, NULL, pkt, len, fd, from, fromlen);
}

void listen_resolve_cb(const unsigned char *pkt, size_t len, size_t slot, int fd, const struct sockaddr *from, socklen_t fromlen, void *user) {
  dispatch_ctx_t *ctx = (dispatch_ctx_t *)user;
  dispatch_datagram(ctx, channel_lookup_by_resolve_slot(ctx->channels, slot), pkt, len, fd, from, fromlen);
}
