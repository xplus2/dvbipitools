/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/demux/rtcp.h"
#include "lib/log.h"
#include "lib/mux/rtcp_build.h"
#include "lib/signal.h"

#include "args.h"
#include "capture/capture.h"
#include "channel/channel.h"
#include "fcc/burst.h"
#include "fcc/burst_table.h"
#include "listen.h"
#include "ret/mcsend.h"
#include "ret/ret.h"
#include "version.h"

#define MC_SEND_TTL 1 /* fixed, no CLI flag */

#define FCC_ASSUMED_MAX_BITRATE_BPS 20000000.0
#define FCC_ASSUMED_TS_PACKET_BYTES 1316.0

static size_t cache_cap_from_gop_ms(unsigned gop_cap_ms) {
  double packets_per_sec = FCC_ASSUMED_MAX_BITRATE_BPS / 8.0 / FCC_ASSUMED_TS_PACKET_BYTES;
  double entries = packets_per_sec * (double)gop_cap_ms / 1000.0;
  return (size_t)entries + 1;
}

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

typedef struct {
  mcsend_table_t *mt; /* NULL: RET disabled or --no-mc-ret */
} ret_send_ctx_t;

static void ret_send_mc_impl(const channel_t *c, const unsigned char *pkt, size_t len, int dscp, void *user) {
  ret_send_ctx_t *ctx = (ret_send_ctx_t *)user;
  mcast_t *m;
  if (!ctx->mt)
    return;
  m = mcsend_get(ctx->mt, (channel_t *)c);
  if (!m)
    return; /* socket not provisioned yet, a NACK can race ahead of capture's first packet */
  mcast_set_tos(m, dscp);
  mcast_send(m, pkt, len);
}

static void ret_send_unicast_impl(int fd, const struct sockaddr *to, socklen_t tolen, const unsigned char *pkt, size_t len, int dscp, void *user) {
  (void)user;
  send_unicast(fd, to, tolen, pkt, len, dscp);
}

typedef struct {
  int fd;
  const struct sockaddr *to;
  socklen_t tolen;
  int congestion; /* set on EAGAIN/ENOBUFS by burst_send_cb, tier-1 502 signal */
} unicast_dest_t;

static void burst_send_cb(const unsigned char *pkt, size_t len, int dscp, void *user) {
  unicast_dest_t *dst = (unicast_dest_t *)user;
  int err = send_unicast(dst->fd, dst->to, dst->tolen, pkt, len, dscp);
  if (err == EAGAIN || err == EWOULDBLOCK || err == ENOBUFS)
    dst->congestion = 1;
}

static void send_rams_i_msn(const unicast_dest_t *dst, uint32_t sender_ssrc, uint32_t media_ssrc, uint8_t msn, uint16_t response, const rtcp_rams_i_tlvs_t *tlvs) {
  unsigned char pkt[128];
  size_t n = rtcp_build_rams_i(sender_ssrc, media_ssrc, msn, response, tlvs, pkt, sizeof pkt);
  if (n > 0)
    send_unicast(dst->fd, dst->to, dst->tolen, pkt, n, RET_DSCP_RTCP);
}

static void send_rams_i(const unicast_dest_t *dst, uint32_t sender_ssrc, uint32_t media_ssrc, uint16_t response, const rtcp_rams_i_tlvs_t *tlvs) {
  send_rams_i_msn(dst, sender_ssrc, media_ssrc, 0, response, tlvs);
}

typedef struct {
  channel_table_t *channels;

  mcsend_table_t *mt; /* NULL: RET disabled or --no-mc-ret */
  unsigned ff_port;
  mcsend_table_t *rsi_mt; /* NULL: default-mode RSI off or riding mt (dvb-rsi-mc-ret) instead */
  int rsi_active; /* RSI on at all, either transport. gates SDES/collision tracking */
  ret_ctx_t *ret; /* NULL: RET disabled */

  burst_table_t *bursts; /* NULL: FCC disabled */
  double burst_multiplier;
  unsigned duration_cap_ms;
  unsigned max_buffer_fill_bound_ms; /* 0 = no bound, see burst_decide() */
  unsigned congestion_nack_threshold; /* 0 = disabled, see nack_cb */
  const cidr_t *fcc_ranges; /* 506: empty = every channel eligible */
  size_t fcc_range_count;
  const cidr_t *fcc_client_ranges; /* 505: empty = every client eligible */
  size_t fcc_client_range_count;
  unsigned char rtx_pt;

  unsigned idle_timeout_s; /* 0 = reaping disabled */
  unsigned ret_client_idle_timeout_s; /* 0 = reaping disabled */

  int nack_truncated_logged; /* re-armed once a NACK arrives that isn't truncated */
} dispatch_ctx_t;

#define CHANNEL_REAP_STEP_SLOTS 8 /* slots checked per packet: bounds cost vs one O(max_channels) sweep per interval */

/* fed by capture.c per whitelisted RTP-carried-TS packet; single demux feeds both RET ring and FCC cache */
static void capture_cb(int family, const void *addr, size_t addr_len, unsigned port, unsigned char dscp, uint32_t ssrc, uint16_t seq, uint32_t timestamp, const unsigned char *payload, size_t payload_len, void *user) {
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
    if (!rc->ctx->nack_truncated_logged) {
      log_line(TOOL_NAME ": NACK has more than %d FCI entries, dropping the rest", RTCP_NACK_MAX_ENTRIES);
      rc->ctx->nack_truncated_logged = 1;
    }
  } else {
    rc->ctx->nack_truncated_logged = 0;
  }
  if (rc->ctx->rsi_active) {
    channel_t *c = rc->resolved ? rc->resolved : channel_find_by_ssrc(rc->ctx->channels, nack->media_ssrc);
    if (c) {
      const char *cname;
      size_t cname_len;
      hned_cname_for(rc, nack->sender_ssrc, &cname, &cname_len);
      channel_hned_seen(rc->ctx->channels, c, nack->sender_ssrc, rc->from, rc->fromlen, cname, cname_len);
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

  c = rc->resolved ? rc->resolved : (req->ignore_media_ssrc ? NULL : channel_find_by_ssrc(rc->ctx->channels, req->media_ssrc));
  if (c && rc->ctx->fcc_range_count > 0 && !in_ranges(c->family, c->addr, rc->ctx->fcc_ranges, rc->ctx->fcc_range_count)) {
    send_rams_i(&dst, req->media_ssrc, req->media_ssrc, (uint16_t)BURST_NOT_ENABLED, NULL);
    return;
  }
  if (c && rc->ctx->rsi_active) {
    const char *cname;
    size_t cname_len;
    hned_cname_for(rc, req->sender_ssrc, &cname, &cname_len);
    channel_hned_seen(rc->ctx->channels, c, req->sender_ssrc, rc->from, rc->fromlen, cname, cname_len);
  }
  resp = burst_decide(c, req, rc->ctx->max_buffer_fill_bound_ms);
  memset(&tlvs, 0, sizeof tlvs);
  if (resp != BURST_ACCEPT) {
    send_rams_i(&dst, req->media_ssrc, req->media_ssrc, (uint16_t)resp, NULL);
    return;
  }

  {
    burst_t *b = burst_new(c, rc->ctx->burst_multiplier, req->has_max_bitrate ? (double)req->max_bitrate_bps : 0.0, rc->ctx->rtx_pt);
    rap_cache_entry_t first;
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

    if (channel_cache_get(c, 0, &first)) {
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
      channel_hned_seen(rc->ctx->channels, c, term->sender_ssrc, rc->from, rc->fromlen, cname, cname_len);
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

/* fires for a recognized-but-corrupt RAMS-T; RAMS-T itself never gets a normal response */
static void malformed_cb(unsigned sfmt, uint32_t sender_ssrc, uint32_t media_ssrc, void *user) {
  listen_req_ctx_t *rc = (listen_req_ctx_t *)user;
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
    rtcp_parse(pkt, len, NULL, NULL, NULL, sdes_cb, NULL, &rc);
  rtcp_parse(pkt, len, ctx->ret ? &nack_cb : NULL, rams_r_cb, ctx->bursts ? &rams_t_cb : NULL, NULL, ctx->bursts ? &malformed_cb : NULL, &rc);
}

static void listen_cb(const unsigned char *pkt, size_t len, int fd, const struct sockaddr *from, socklen_t fromlen, void *user) {
  dispatch_datagram((dispatch_ctx_t *)user, NULL, pkt, len, fd, from, fromlen);
}

static void listen_resolve_cb(const unsigned char *pkt, size_t len, size_t slot, int fd, const struct sockaddr *from, socklen_t fromlen, void *user) {
  dispatch_ctx_t *ctx = (dispatch_ctx_t *)user;
  dispatch_datagram(ctx, channel_lookup_by_resolve_slot(ctx->channels, slot), pkt, len, fd, from, fromlen);
}

typedef struct {
  burst_table_t *bursts;
  unsigned duration_cap_ms;
} pacer_ctx_t;

typedef struct {
  size_t idx;
  int fd;
  struct sockaddr_storage addr;
  socklen_t addrlen;
  burst_t *b;
} pacer_snap_t;

/* under pc->bursts->lock: if this pacer still owns the slot, clear it on congestion/done.
   returns 1 if this pacer owned the slot (caller may still need to send a notice) */
static int finalize_burst_slot(pacer_ctx_t *pc, size_t idx, burst_t *b, int congestion, burst_tick_result_t r, int *remove_slot) {
  int owns_slot = 0;
  pthread_mutex_lock(&pc->bursts->lock);
  if (pc->bursts->slots[idx].in_use && pc->bursts->slots[idx].b == b) {
    owns_slot = 1;
    if (congestion || r == BURST_TICK_DONE) {
      pc->bursts->slots[idx].b = NULL;
      pc->bursts->slots[idx].in_use = 0;
      *remove_slot = 1;
    }
  }
  pthread_mutex_unlock(&pc->bursts->lock);
  return owns_slot;
}

/* lock covers only slot scan/copy and done-cleanup, never sends below */
static void *pacer_main(void *arg) {
  pacer_ctx_t *pc = (pacer_ctx_t *)arg;
  struct timespec tick = {0, 20 * 1000 * 1000}; /* 20ms */
  pacer_snap_t *snap = calloc(pc->bursts->cap, sizeof *snap);

  if (!snap) {
    log_line(TOOL_NAME ": pacer: out of memory, burst pacing disabled");
    return NULL;
  }

  while (!signal_stop_requested()) {
    size_t i, n = 0;

    nanosleep(&tick, NULL);

    pthread_mutex_lock(&pc->bursts->lock);
    for (i = 0; i < pc->bursts->cap; i++) {
      burst_slot_t *slot = &pc->bursts->slots[i];
      if (!slot->in_use)
        continue;
      burst_acquire(slot->b);
      snap[n].idx = i;
      snap[n].fd = slot->fd;
      snap[n].addr = slot->addr;
      snap[n].addrlen = slot->addrlen;
      snap[n].b = slot->b;
      n++;
    }
    pthread_mutex_unlock(&pc->bursts->lock);

    for (i = 0; i < n; i++) {
      unicast_dest_t dst;
      burst_tick_result_t r;
      int owns_slot = 0;
      int remove_slot = 0;

      dst.fd = snap[i].fd;
      dst.to = (const struct sockaddr *)&snap[i].addr;
      dst.tolen = snap[i].addrlen;
      dst.congestion = 0;
      r = burst_tick(snap[i].b, pc->duration_cap_ms, burst_send_cb, &dst);
      owns_slot = finalize_burst_slot(pc, snap[i].idx, snap[i].b, dst.congestion, r, &remove_slot);

      if (owns_slot && dst.congestion)
        send_rams_i(&dst, 0, 0, (uint16_t)BURST_CONGESTION, NULL);
      else if (owns_slot && r == BURST_TICK_DONE)
        send_rams_i(&dst, 0, 0, (uint16_t)BURST_DONE, NULL);

      if (remove_slot)
        burst_release(snap[i].b); /* drop slot ownership */
      burst_release(snap[i].b); /* drop pacer snapshot */
    }
  }
  free(snap);
  return NULL;
}

typedef struct {
  channel_table_t *channels;
  ret_send_ctx_t *send_ctx; /* wraps RSI-dedicated mcsend table, not RET session's */
  unsigned interval_s;
  unsigned char addr[4]; /* IPv4 only. F.5.3 SRBT 1 (IPv6) "not supported in DVB" */
  uint16_t port; /* shared -l port, unused if resolve_by_port */
  const char *hostname; /* NULL/0-length: SRBT 0 (addr above). set: SRBT 2 instead */
  size_t hostname_len;
  int resolve_by_port; /* announce each channel's own resolve_slot port instead of port above */
  unsigned resolve_base_port;
} rsi_pacer_ctx_t;

static void ntp_now(uint32_t *sec, uint32_t *frac) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  *sec = (uint32_t)ts.tv_sec + 2208988800u; /* unix epoch (1970) to NTP epoch (1900) */
  *frac = (uint32_t)((double)ts.tv_nsec / 1e9 * 4294967296.0);
}

#define RSI_PKT_MAX 320 /* header(20) + dns(256 worst case) + bandwidth(8) + collision(4+4*CHANNEL_HNED_COLLISION_MAX) */
#define RSI_BANDWIDTH_FRACTION 0.05 /* F.4.3 rtcp-bandwidth default: 5% of stream bandwidth */

/* F.5.3: ssrc/summarized-ssrc = channel's own media ssrc, not a separate identity.
   sent on original session (own group:port), not MC RET, per F.5.3 default. */
static void *rsi_pacer_main(void *arg) {
  rsi_pacer_ctx_t *pc = (rsi_pacer_ctx_t *)arg;
  struct timespec chunk = {0, 200 * 1000 * 1000}; /* 200ms: stop signal noticed promptly */
  unsigned chunks_per_cycle = pc->interval_s * 5;
  time_t collision_max_age = (time_t)pc->interval_s * 3; /* report collision ~3 cycles after last seen */

  if (!chunks_per_cycle)
    chunks_per_cycle = 1;

  while (!signal_stop_requested()) {
    unsigned i;
    size_t cap, idx;
    uint32_t ntp_sec, ntp_frac;

    for (i = 0; i < chunks_per_cycle && !signal_stop_requested(); i++)
      nanosleep(&chunk, NULL);
    if (signal_stop_requested())
      break;

    ntp_now(&ntp_sec, &ntp_frac);
    cap = channel_table_capacity(pc->channels);
    for (idx = 0; idx < cap; idx++) {
      channel_t *c = channel_table_at(pc->channels, idx);
      unsigned char pkt[RSI_PKT_MAX];
      uint32_t collisions[CHANNEL_HNED_COLLISION_MAX];
      size_t off, sub_len, collision_n;
      uint32_t ssrc;
      double nominal_bps;
      uint16_t port;

      if (!c || !atomic_load_explicit(&c->ssrc_known, memory_order_acquire))
        continue;
      ssrc = atomic_load_explicit(&c->ssrc, memory_order_acquire);
      port = pc->resolve_by_port ? (uint16_t)(pc->resolve_base_port + c->resolve_slot) : pc->port;

      off = 20;
      if (pc->hostname_len > 0)
        sub_len = rtcp_build_rsi_srbt_dns(pc->hostname, pc->hostname_len, port, pkt + off, sizeof(pkt) - off);
      else
        sub_len = rtcp_build_rsi_srbt_addr(pc->addr, sizeof pc->addr, port, pkt + off, sizeof(pkt) - off);
      if (sub_len == 0)
        continue;
      off += sub_len;

      nominal_bps = atomic_load_explicit(&c->nominal_bps, memory_order_relaxed);
      if (nominal_bps > 0.0) {
        sub_len = rtcp_build_rsi_srbt_bandwidth(nominal_bps * RSI_BANDWIDTH_FRACTION / 1000.0, pkt + off, sizeof(pkt) - off);
        off += sub_len; /* 0 on out-of-range kbps: sub-report just omitted, off unchanged */
      }

      collision_n = channel_hned_collisions(pc->channels, c, collisions, CHANNEL_HNED_COLLISION_MAX, collision_max_age);
      if (collision_n > 0) {
        sub_len = rtcp_build_rsi_srbt_collision(collisions, collision_n, pkt + off, sizeof(pkt) - off);
        off += sub_len;
      }

      if (rtcp_build_rsi_header(ssrc, ssrc, ntp_sec, ntp_frac, off, pkt, sizeof pkt) == 0)
        continue;
      ret_send_mc_impl(c, pkt, off, RET_DSCP_RTCP, pc->send_ctx);
    }
  }
  return NULL;
}

int main(int argc, char **argv) {
  config_t cfg;
  args_status_t st;
  size_t max_channels, ring_slots, cache_cap;
  channel_table_t *channels;
  mcsend_table_t *mt = NULL;
  mcsend_table_t *rsi_mt = NULL;
  ret_ctx_t *ret = NULL;
  burst_table_t *bursts = NULL;
  capture_t *cap;
  char errbuf[256];
  ret_send_ctx_t ret_send_ctx;
  ret_send_ctx_t rsi_send_ctx;
  dispatch_ctx_t dispatch_ctx;
  listen_pool_t *pool;
  listen_multi_t *resolve_pool = NULL;
  unsigned resolve_base_port;
  pacer_ctx_t pacer_ctx;
  pthread_t pacer_thread;
  int pacer_started = 0;
  rsi_pacer_ctx_t rsi_ctx;
  pthread_t rsi_thread;
  int rsi_started = 0;

  log_set_color(log_color_prescan(argc, argv));
  log_line_ansi("\e[1m%s\e[0m \e[0;32mv%s\e[0m \e[0;37m%s\e[0m \e[0;37m%s\e[0m \e[0;34m%s\e[0m", TOOL_NAME, TOOL_VERSION, BUILD_ARCH, BUILD_TYPE, BUILD_LINK);
  st = args_parse(argc, argv, &cfg);
  if (st == ARGS_OK)
    log_set_color((log_color_t)cfg.color_mode);
  if (st == ARGS_HELP)
    return 0;
  if (st == ARGS_ERR) {
    fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME);
    return 2;
  }
  if (cfg.daemonize && daemon(1, 1) != 0) {
    log_line(TOOL_NAME ": daemonize failed: %s", strerror(errno));
    return 1;
  }

  max_channels = cfg.max_channels ? cfg.max_channels : CHANNEL_DEFAULT_MAX;
  ring_slots = cfg.no_ret ? 0 : cfg.buffer_ms; /* ring_slots ~= buffer_ms: ~1 packet/ms assumption */
  cache_cap = cfg.no_fcc ? 0 : cache_cap_from_gop_ms(cfg.gop_cap_ms);
  channels = channel_table_new(max_channels, ring_slots, cache_cap);
  if (!channels) {
    fprintf(stderr, "%s: out of memory allocating channel table\n", TOOL_NAME);
    return 1;
  }

  if (!cfg.no_ret && !cfg.no_mc_ret) {
    mt = mcsend_table_new(max_channels, cfg.iface, MC_SEND_TTL);
    if (!mt) {
      fprintf(stderr, "%s: out of memory allocating MC RET session table\n", TOOL_NAME);
      return 1;
    }
  }
  if (!cfg.no_ret && !cfg.no_rsi) {
    if (cfg.listen_family == AF_INET6) {
      log_line(TOOL_NAME ": RSI self-announcement needs an IPv4 -l address (F.5.3 IPv6 unicast feedback is not supported in DVB), disabling it");
    } else if (cfg.rsi_mc_ret) {
      /* dvb-rsi-mc-ret: RSI rides mt itself (F.6.2.2 same group:port), no separate socket */
    } else {
      rsi_mt = mcsend_table_new(max_channels, cfg.iface, MC_SEND_TTL);
      if (!rsi_mt) {
        fprintf(stderr, "%s: out of memory allocating RSI announcement table\n", TOOL_NAME);
        return 1;
      }
    }
  }
  if (!cfg.no_fcc) {
    bursts = burst_table_new(cfg.max_bursts);
    if (!bursts) {
      fprintf(stderr, "%s: out of memory allocating burst table\n", TOOL_NAME);
      return 1;
    }
  }

  cap = capture_open(cfg.iface, cfg.range_ptrs, cfg.range_count, errbuf, sizeof errbuf);
  if (!cap) {
    fprintf(stderr, "%s: %s\n", TOOL_NAME, errbuf);
    return 1;
  }
  if (capture_drop_privileges(cfg.user) != 0) {
    fprintf(stderr, "%s: failed to drop privileges to -u %s\n", TOOL_NAME, cfg.user);
    return 1;
  }

  if (!cfg.no_ret) {
    ret_send_ctx.mt = mt;
    ret = ret_ctx_new(channels, cfg.rtx_pt, cfg.max_ret_clients, ret_send_mc_impl, ret_send_unicast_impl, &ret_send_ctx);
    if (!ret) {
      fprintf(stderr, "%s: out of memory creating ret context\n", TOOL_NAME);
      return 1;
    }
  }

  dispatch_ctx.channels = channels;
  dispatch_ctx.mt = mt;
  dispatch_ctx.ff_port = cfg.ff_port;
  dispatch_ctx.rsi_mt = rsi_mt;
  dispatch_ctx.rsi_active = rsi_mt != NULL || (cfg.rsi_mc_ret && mt != NULL);
  dispatch_ctx.ret = ret;
  dispatch_ctx.bursts = bursts;
  dispatch_ctx.burst_multiplier = cfg.burst_multiplier;
  dispatch_ctx.duration_cap_ms = cfg.duration_cap_ms;
  dispatch_ctx.max_buffer_fill_bound_ms = cfg.max_buffer_fill_bound_ms;
  dispatch_ctx.congestion_nack_threshold = cfg.congestion_nack_threshold;
  dispatch_ctx.fcc_ranges = cfg.fcc_ranges;
  dispatch_ctx.fcc_range_count = cfg.fcc_range_count;
  dispatch_ctx.fcc_client_ranges = cfg.fcc_client_ranges;
  dispatch_ctx.fcc_client_range_count = cfg.fcc_client_range_count;
  dispatch_ctx.rtx_pt = cfg.rtx_pt;
  dispatch_ctx.idle_timeout_s = cfg.channel_idle_timeout_s;
  dispatch_ctx.ret_client_idle_timeout_s = cfg.ret_client_idle_timeout_s;
  dispatch_ctx.nack_truncated_logged = 0;

  pool = listen_pool_start(cfg.listen_family, cfg.listen_addr, cfg.listen_port, cfg.workers, listen_cb, &dispatch_ctx);
  if (!pool) {
    fprintf(stderr, "%s: failed to start listen workers on %s:%u\n", TOOL_NAME, cfg.listen_addr, cfg.listen_port);
    return 1;
  }

  resolve_base_port = cfg.fcc_resolve_base_port ? cfg.fcc_resolve_base_port : cfg.listen_port + 1;
  if (cfg.fcc_resolve_by_port) {
    resolve_pool = listen_multi_start(cfg.listen_family, cfg.listen_addr, resolve_base_port, max_channels, listen_resolve_cb, &dispatch_ctx);
    if (!resolve_pool) {
      fprintf(stderr, "%s: failed to start FCC resolve-by-port sockets at %s:%u..%u\n", TOOL_NAME, cfg.listen_addr, resolve_base_port, resolve_base_port + (unsigned)max_channels - 1);
      return 1;
    }
  }

  if (bursts) {
    pacer_ctx.bursts = bursts;
    pacer_ctx.duration_cap_ms = cfg.duration_cap_ms;
    if (pthread_create(&pacer_thread, NULL, pacer_main, &pacer_ctx) != 0) {
      fprintf(stderr, "%s: failed to start burst pacing thread\n", TOOL_NAME);
      return 1;
    }
    pacer_started = 1;
  }

  if (rsi_mt || (cfg.rsi_mc_ret && mt)) {
    if (cfg.rsi_mc_ret) {
      rsi_ctx.send_ctx = &ret_send_ctx; /* dvb-rsi-mc-ret: rides mt, F.6.2.2 same group:port */
    } else {
      rsi_send_ctx.mt = rsi_mt;
      rsi_ctx.send_ctx = &rsi_send_ctx;
    }
    rsi_ctx.channels = channels;
    rsi_ctx.interval_s = cfg.rsi_interval_s;
    rsi_ctx.port = (uint16_t)cfg.listen_port;
    rsi_ctx.resolve_by_port = cfg.fcc_resolve_by_port;
    rsi_ctx.resolve_base_port = resolve_base_port;
    rsi_ctx.hostname = cfg.rsi_hostname[0] ? cfg.rsi_hostname : NULL;
    rsi_ctx.hostname_len = strlen(cfg.rsi_hostname);
    if (inet_pton(AF_INET, cfg.listen_addr, rsi_ctx.addr) != 1) {
      fprintf(stderr, "%s: failed to parse -l address for RSI announcement\n", TOOL_NAME);
      return 1;
    }
    if (pthread_create(&rsi_thread, NULL, rsi_pacer_main, &rsi_ctx) != 0) {
      fprintf(stderr, "%s: failed to start RSI announcement thread\n", TOOL_NAME);
      return 1;
    }
    rsi_started = 1;
  }

  signals_install();
  log_line(TOOL_NAME ": capturing, %u worker(s) on %s:%u, %zu channel slots [%s%s%s%s]", cfg.workers, cfg.listen_addr, cfg.listen_port, max_channels,
      cfg.no_ret ? "no RET" : (mt ? "RET+MC" : "RET unicast-only"), cfg.no_ret || cfg.no_fcc ? "" : ", ", cfg.no_fcc ? "no FCC" : "FCC", rsi_started ? "+RSI" : "");
  capture_run(cap, capture_cb, &dispatch_ctx);
  capture_close(cap);
  if (pacer_started)
    pthread_join(pacer_thread, NULL);
  if (rsi_started)
    pthread_join(rsi_thread, NULL);
  listen_pool_stop(pool);
  if (resolve_pool)
    listen_multi_stop(resolve_pool);
  if (ret)
    ret_ctx_free(ret);
  if (bursts)
    burst_table_free(bursts);
  if (mt)
    mcsend_table_free(mt);
  if (rsi_mt)
    mcsend_table_free(rsi_mt);
  channel_table_free(channels);
  return 0;
}
