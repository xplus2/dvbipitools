/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_RUN_RUN_H
#define DIPIFCCRET_RUN_RUN_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#include "lib/metrics/export.h"
#include "lib/mux/rtcp_build.h"

#include "../args.h"
#include "../capture/capture.h"
#include "../channel/channel.h"
#include "../fcc/burst_table.h"
#include "../ret/mcsend.h"
#include "../ret/ret.h"

typedef struct {
  int fd;
  const struct sockaddr *to;
  socklen_t tolen;
  int congestion; /* set on EAGAIN/ENOBUFS by burst_send_cb, tier-1 502 signal */
} unicast_dest_t;

void burst_send_cb(const unsigned char *pkt, size_t len, int dscp, void *user);
void send_rams_i_msn(const unicast_dest_t *dst, uint32_t sender_ssrc, uint32_t media_ssrc, uint8_t msn, uint16_t response, const rtcp_rams_i_tlvs_t *tlvs);
void send_rams_i(const unicast_dest_t *dst, uint32_t sender_ssrc, uint32_t media_ssrc, uint16_t response, const rtcp_rams_i_tlvs_t *tlvs);

typedef struct {
  mcsend_table_t *mt; /* NULL: RET disabled or --no-mc-ret */
} ret_send_ctx_t;

void ret_send_mc_impl(const channel_t *c, const unsigned char *pkt, size_t len, int dscp, void *user);
void ret_send_unicast_impl(int fd, const struct sockaddr *to, socklen_t tolen, const unsigned char *pkt, size_t len, int dscp, void *user);

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

  atomic_int nack_truncated_logged; /* re-armed on next non-truncated NACK. racy across workers, relaxed ok */
} dispatch_ctx_t;

void capture_cb(int family, const void *addr, size_t addr_len, unsigned port, unsigned char dscp, uint32_t ssrc, uint16_t seq, uint32_t timestamp,
                 const unsigned char *payload, size_t payload_len, void *user);
void listen_cb(const unsigned char *pkt, size_t len, int fd, const struct sockaddr *from, socklen_t fromlen, void *user);
void listen_resolve_cb(const unsigned char *pkt, size_t len, size_t slot, int fd, const struct sockaddr *from, socklen_t fromlen, void *user);

typedef struct {
  burst_table_t *bursts;
  unsigned duration_cap_ms;
} pacer_ctx_t;

void *pacer_main(void *arg);

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

void *rsi_pacer_main(void *arg);

typedef struct {
  metrics_exporter_t *mx;
  channel_table_t *channels;
  ret_ctx_t *ret;        /* NULL: RET disabled */
  burst_table_t *bursts; /* NULL: FCC disabled */
} metrics_ctx_t;

void *metrics_thread_main(void *arg);

#endif
