/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

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
#include "capture.h"
#include "channel.h"
#include "fcc/burst.h"
#include "listen.h"
#include "ret/mcsend.h"
#include "ret/ret.h"
#include "version.h"

#define MC_SEND_TTL 1 /* fixed, no CLI flag - matches dipitvhead's own default */

#define FCC_ASSUMED_MAX_BITRATE_BPS 20000000.0
#define FCC_ASSUMED_TS_PACKET_BYTES 1316.0

static size_t cache_cap_from_gop_ms(unsigned gop_cap_ms) {
  double packets_per_sec = FCC_ASSUMED_MAX_BITRATE_BPS / 8.0 / FCC_ASSUMED_TS_PACKET_BYTES;
  double entries = packets_per_sec * (double)gop_cap_ms / 1000.0;
  return (size_t)entries + 1;
}

static void send_unicast(int fd, const struct sockaddr *to, socklen_t tolen, const unsigned char *pkt, size_t len, int dscp) {
  if (to->sa_family == AF_INET6)
    setsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &dscp, sizeof dscp);
  else
    setsockopt(fd, IPPROTO_IP, IP_TOS, &dscp, sizeof dscp);
  if (sendto(fd, pkt, len, 0, to, tolen) < 0)
    log_line(TOOL_NAME ": unicast sendto: %s", strerror(errno));
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
  int in_use;
  struct sockaddr_storage addr;
  socklen_t addrlen;
  int fd;
  burst_t *b;
} burst_slot_t;

typedef struct {
  burst_slot_t *slots;
  size_t cap;
  pthread_mutex_t lock;
} burst_table_t;

static burst_table_t *burst_table_new(size_t cap) {
  burst_table_t *t = calloc(1, sizeof *t);
  if (!t)
    return NULL;
  t->slots = calloc(cap, sizeof *t->slots);
  if (!t->slots) {
    free(t);
    return NULL;
  }
  t->cap = cap;
  pthread_mutex_init(&t->lock, NULL);
  return t;
}

static void burst_table_free(burst_table_t *t) {
  size_t i;
  if (!t)
    return;
  for (i = 0; i < t->cap; i++)
    if (t->slots[i].in_use)
      burst_free(t->slots[i].b);
  pthread_mutex_destroy(&t->lock);
  free(t->slots);
  free(t);
}

static int sockaddr_eq(const struct sockaddr_storage *a, socklen_t alen, const struct sockaddr *b, socklen_t blen) {
  if (alen != blen || a->ss_family != b->sa_family)
    return 0;
  if (b->sa_family == AF_INET) {
    const struct sockaddr_in *ba = (const struct sockaddr_in *)b;
    const struct sockaddr_in *aa = (const struct sockaddr_in *)a;
    return aa->sin_port == ba->sin_port && aa->sin_addr.s_addr == ba->sin_addr.s_addr;
  }
  if (b->sa_family == AF_INET6) {
    const struct sockaddr_in6 *ba = (const struct sockaddr_in6 *)b;
    const struct sockaddr_in6 *aa = (const struct sockaddr_in6 *)a;
    return aa->sin6_port == ba->sin6_port && memcmp(&aa->sin6_addr, &ba->sin6_addr, sizeof aa->sin6_addr) == 0;
  }
  return 0;
}

/* NULL if table full (logged) - caller should reject, not retry */
static burst_slot_t *burst_table_claim(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen, int fd, burst_t *b) {
  size_t i;
  pthread_mutex_lock(&t->lock);
  for (i = 0; i < t->cap; i++) {
    if (!t->slots[i].in_use) {
      memcpy(&t->slots[i].addr, addr, addrlen);
      t->slots[i].addrlen = addrlen;
      t->slots[i].fd = fd;
      t->slots[i].b = b;
      t->slots[i].in_use = 1;
      pthread_mutex_unlock(&t->lock);
      return &t->slots[i];
    }
  }
  pthread_mutex_unlock(&t->lock);
  log_line(TOOL_NAME ": max-bursts (%zu) reached, rejecting new burst session", t->cap);
  return NULL;
}

/* terminates matching session if found; 1 found, 0 not */
static int burst_table_terminate(burst_table_t *t, const struct sockaddr *addr, socklen_t addrlen) {
  size_t i;
  pthread_mutex_lock(&t->lock);
  for (i = 0; i < t->cap; i++) {
    if (t->slots[i].in_use && sockaddr_eq(&t->slots[i].addr, t->slots[i].addrlen, addr, addrlen)) {
      burst_terminate(t->slots[i].b);
      pthread_mutex_unlock(&t->lock);
      return 1;
    }
  }
  pthread_mutex_unlock(&t->lock);
  return 0;
}

typedef struct {
  int fd;
  const struct sockaddr *to;
  socklen_t tolen;
} unicast_dest_t;

static void burst_send_cb(const unsigned char *pkt, size_t len, void *user) {
  const unicast_dest_t *dst = (const unicast_dest_t *)user;
  send_unicast(dst->fd, dst->to, dst->tolen, pkt, len, RET_DSCP_RTX);
}

static void send_rams_i(const unicast_dest_t *dst, uint32_t sender_ssrc, uint32_t media_ssrc, uint16_t response, const rtcp_rams_i_tlvs_t *tlvs) {
  unsigned char pkt[128];
  size_t n = rtcp_build_rams_i(sender_ssrc, media_ssrc, 0, response, tlvs, pkt, sizeof pkt);
  if (n > 0)
    send_unicast(dst->fd, dst->to, dst->tolen, pkt, n, RET_DSCP_RTCP);
}

typedef struct {
  channel_table_t *channels;

  mcsend_table_t *mt; /* NULL: RET disabled or --no-mc-ret */
  unsigned ff_port;
  ret_ctx_t *ret; /* NULL: RET disabled */

  burst_table_t *bursts; /* NULL: FCC disabled */
  double burst_multiplier;
  unsigned duration_cap_ms;
  unsigned char rtx_pt;

  unsigned idle_timeout_s; /* 0 = reaping disabled */

  int nack_truncated_logged; /* re-armed once a NACK arrives that isn't truncated */
} dispatch_ctx_t;

#define CHANNEL_REAP_STEP_SLOTS 8 /* slots checked per packet - bounds reap cost instead of one O(max_channels) sweep per interval */

/* fed by capture.c per whitelisted RTP-carried-TS packet; single demux feeds both RET ring and FCC cache */
static void capture_cb(int family, const void *addr, size_t addr_len, unsigned port, uint32_t ssrc, uint16_t seq, uint32_t timestamp, const unsigned char *payload, size_t payload_len, void *user) {
  dispatch_ctx_t *ctx = (dispatch_ctx_t *)user;
  channel_t *c;

  if (ctx->idle_timeout_s)
    channel_table_reap_step(ctx->channels, (time_t)ctx->idle_timeout_s, CHANNEL_REAP_STEP_SLOTS);

  c = channel_lookup(ctx->channels, family, addr, addr_len, port);
  if (!c)
    return; /* max-channels cap, already logged by channel_lookup */
  channel_store(ctx->channels, c, ssrc, seq, timestamp, payload, payload_len);
  if (ctx->mt)
    mcsend_ensure(ctx->mt, c, ctx->ff_port); /* cheap no-op if c already has a socket */
}

typedef struct {
  dispatch_ctx_t *ctx;
  int fd;
  const struct sockaddr *from;
  socklen_t fromlen;
} listen_req_ctx_t;

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
  ret_handle_nack(rc->ctx->ret, nack, rc->fd, rc->from, rc->fromlen);
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
  c = req->ignore_media_ssrc ? NULL : channel_find_by_ssrc(rc->ctx->channels, req->media_ssrc);
  resp = burst_decide(c, req);
  memset(&tlvs, 0, sizeof tlvs);
  if (resp != BURST_ACCEPT) {
    send_rams_i(&dst, req->media_ssrc, req->media_ssrc, (uint16_t)resp, NULL);
    return;
  }

  {
    burst_t *b = burst_new(c, rc->ctx->burst_multiplier, req->has_max_bitrate ? (double)req->max_bitrate_bps : 0.0, rc->ctx->rtx_pt);
    rap_cache_entry_t first;
    burst_slot_t *slot;
    if (!b) {
      send_rams_i(&dst, req->media_ssrc, req->media_ssrc, (uint16_t)BURST_REJECT, NULL);
      return;
    }
    slot = burst_table_claim(rc->ctx->bursts, rc->from, rc->fromlen, rc->fd, b);
    if (!slot) {
      burst_free(b);
      send_rams_i(&dst, req->media_ssrc, req->media_ssrc, (uint16_t)BURST_REJECT, NULL);
      return;
    }

    if (channel_cache_get(c, 0, &first)) {
      tlvs.has_first_packet_seqnum = 1;
      tlvs.first_packet_seqnum = first.seq;
    }
    tlvs.has_burst_duration = 1;
    tlvs.burst_duration_ms = rc->ctx->duration_cap_ms;
    tlvs.has_max_transmit_bitrate = 1;
    tlvs.max_transmit_bitrate_bps = (uint64_t)b->target_bps;

    send_rams_i(&dst, req->media_ssrc, req->media_ssrc, (uint16_t)BURST_ACCEPT, &tlvs);
  }
}

static void rams_t_cb(const rtcp_rams_t_t *term, void *user) {
  listen_req_ctx_t *rc = (listen_req_ctx_t *)user;
  (void)term;
  burst_table_terminate(rc->ctx->bursts, rc->from, rc->fromlen);
}

/* one datagram, one rtcp_parse, feeds RET NACK and/or FCC RAMS-R/RAMS-T - not parsed twice per feature */
static void listen_cb(const unsigned char *pkt, size_t len, int fd, const struct sockaddr *from, socklen_t fromlen, void *user) {
  dispatch_ctx_t *ctx = (dispatch_ctx_t *)user;
  listen_req_ctx_t rc;
  rc.ctx = ctx;
  rc.fd = fd;
  rc.from = from;
  rc.fromlen = fromlen;
  rtcp_parse(pkt, len, ctx->ret ? nack_cb : NULL, ctx->bursts ? rams_r_cb : NULL, ctx->bursts ? rams_t_cb : NULL, &rc);
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

/* lock covers only the slot scan/copy and done-cleanup, never the sends below */
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

      dst.fd = snap[i].fd;
      dst.to = (const struct sockaddr *)&snap[i].addr;
      dst.tolen = snap[i].addrlen;
      r = burst_tick(snap[i].b, pc->duration_cap_ms, burst_send_cb, &dst);
      if (r == BURST_TICK_DONE) {
        send_rams_i(&dst, 0, 0, 201, NULL);
        pthread_mutex_lock(&pc->bursts->lock);
        burst_free(snap[i].b);
        pc->bursts->slots[snap[i].idx].in_use = 0;
        pthread_mutex_unlock(&pc->bursts->lock);
      }
    }
  }
  free(snap);
  return NULL;
}

int main(int argc, char **argv) {
  config_t cfg;
  args_status_t st;
  size_t max_channels, ring_slots, cache_cap;
  channel_table_t *channels;
  mcsend_table_t *mt = NULL;
  ret_ctx_t *ret = NULL;
  burst_table_t *bursts = NULL;
  capture_t *cap;
  char errbuf[256];
  ret_send_ctx_t ret_send_ctx;
  dispatch_ctx_t dispatch_ctx;
  listen_pool_t *pool;
  pacer_ctx_t pacer_ctx;
  pthread_t pacer_thread;
  int pacer_started = 0;

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
    ret = ret_ctx_new(channels, cfg.rtx_pt, ret_send_mc_impl, ret_send_unicast_impl, &ret_send_ctx);
    if (!ret) {
      fprintf(stderr, "%s: out of memory creating ret context\n", TOOL_NAME);
      return 1;
    }
  }

  dispatch_ctx.channels = channels;
  dispatch_ctx.mt = mt;
  dispatch_ctx.ff_port = cfg.ff_port;
  dispatch_ctx.ret = ret;
  dispatch_ctx.bursts = bursts;
  dispatch_ctx.burst_multiplier = cfg.burst_multiplier;
  dispatch_ctx.duration_cap_ms = cfg.duration_cap_ms;
  dispatch_ctx.rtx_pt = cfg.rtx_pt;
  dispatch_ctx.idle_timeout_s = cfg.channel_idle_timeout_s;
  dispatch_ctx.nack_truncated_logged = 0;

  pool = listen_pool_start(cfg.listen_family, cfg.listen_addr, cfg.listen_port, cfg.workers, listen_cb, &dispatch_ctx);
  if (!pool) {
    fprintf(stderr, "%s: failed to start listen workers on %s:%u\n", TOOL_NAME, cfg.listen_addr, cfg.listen_port);
    return 1;
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

  signals_install();
  log_line(TOOL_NAME ": capturing, %u worker(s) on %s:%u, %zu channel slots [%s%s%s]", cfg.workers, cfg.listen_addr, cfg.listen_port, max_channels,
      cfg.no_ret ? "no RET" : (mt ? "RET+MC" : "RET unicast-only"), cfg.no_ret || cfg.no_fcc ? "" : ", ", cfg.no_fcc ? "no FCC" : "FCC");
  capture_run(cap, capture_cb, &dispatch_ctx);
  capture_close(cap);
  if (pacer_started)
    pthread_join(pacer_thread, NULL);
  listen_pool_stop(pool);
  if (ret)
    ret_ctx_free(ret);
  if (bursts)
    burst_table_free(bursts);
  if (mt)
    mcsend_table_free(mt);
  channel_table_free(channels);
  return 0;
}
