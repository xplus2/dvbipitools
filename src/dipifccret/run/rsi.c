/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdatomic.h>
#include <time.h>

#include "lib/mux/rtcp_build.h"
#include "lib/signal.h"

#include "run.h"

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
void *rsi_pacer_main(void *arg) {
  rsi_pacer_ctx_t *pc = (rsi_pacer_ctx_t *)arg;
  struct timespec chunk = {0, 200 * 1000 * 1000}; /* 200ms: stop signal noticed promptly */
  unsigned chunks_per_cycle = pc->interval_s * 5;
  time_t collision_max_age = (time_t)pc->interval_s * 3; /* report collision ~3 cycles after last seen */

  if (!chunks_per_cycle)
    chunks_per_cycle = 1;

  while (!signal_stop_requested()) {
    size_t cap;
    uint32_t ntp_sec, ntp_frac;

    for (unsigned i = 0; i < chunks_per_cycle && !signal_stop_requested(); i++)
      nanosleep(&chunk, NULL);
    if (signal_stop_requested())
      break;

    ntp_now(&ntp_sec, &ntp_frac);
    cap = channel_table_capacity(pc->channels);
    for (size_t idx = 0; idx < cap; idx++) {
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

      collision_n = channel_hned_collisions(c, collisions, CHANNEL_HNED_COLLISION_MAX, collision_max_age);
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
