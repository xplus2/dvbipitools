/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_RIST_RISTPEER_H
#define DVBIPITOOLS_LIB_NET_RIST_RISTPEER_H

#include <stdint.h>
#include <stddef.h>

#include "lib/metrics/export.h"

struct rist_ctx;
struct rist_stats;

#define RIST_RECEIVER_FLOW_ENTRIES 6
#define RIST_READ_TIMEOUT_MS 200
#define RIST_STATS_INTERVAL_MS 1000 /* metrics_exporter_due() gates actual push cadence */

/* listening peer: data port (and rtcp_port: +1 sibling, simple profile) must be free.
   librist crash in rist_peer_create() on taken rtcp_port. 1 ok, 0 taken */
int ristpeer_listen_ports_usable(const char *uri, int rtcp_port);

/* key_size 128/256, 0 = URI/lib default. rtcp_port: simple profile, listening
   0 ok, -1 rist_parse_address2/rist_peer_create failed (logged) */
int ristpeer_add(struct rist_ctx *ctx, const char *peer_uri, const char *secret, int key_size, const char *cname, unsigned buffer_ms, int initiate_conn, int rtcp_port);

typedef struct {
  const char *const *uris; /* listening peers, bonded when n > 1 */
  unsigned n;
  int simple;              /* simple profile, else main */
  const char *secret;
  int key_size;
  const char *cname;
  unsigned buffer_ms;
  int verbose;
  int (*stats_cb)(void *arg, const struct rist_stats *stats); /* NULL = none */
  void *stats_arg;
} rist_recv_cfg_t;

/* started receiver ctx, all peers added. NULL on failure (logged). rist_destroy() by caller */
struct rist_ctx *rist_receiver_open(const rist_recv_cfg_t *cfg);

/* stale-flow watchdog for rist_receiver_data_read2(RIST_READ_TIMEOUT_MS) loops */
typedef struct {
  unsigned idle_ms;
  unsigned reset_ms;
  int got;
  uint32_t flow_id;
  double last;
} ristpeer_flow_watch_t;

void ristpeer_flow_watch_init(ristpeer_flow_watch_t *w, unsigned buffer_ms);

/* data block arrived. 1: deliver. 0: foreign flow, drop. first flow is locked until it goes stale */
int ristpeer_flow_watch_data(ristpeer_flow_watch_t *w, uint32_t flow_id);

/* read timed out. 1: flow went stale, caller restarts ctx (watch rearmed) */
int ristpeer_flow_watch_idle(ristpeer_flow_watch_t *w);

/* receiver flow stats to out[RIST_RECEIVER_FLOW_ENTRIES]. 0 and out untouched: other stats type */
size_t ristpeer_receiver_flow_entries(const struct rist_stats *stats, metrics_entry_t *out);

/* pushes entries if metrics_exporter_due(), then rist_stats_free(stats). always returns 0 */
void ristpeer_push_stats_if_due(metrics_exporter_t *mx, const char *tool_version, const metrics_entry_t *entries, size_t n, const struct rist_stats *stats);

#endif
