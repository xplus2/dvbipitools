/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <librist/librist.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/signal.h"

#include "ristpeer.h"

int rist_add_peer(struct rist_ctx *ctx, const char *peer_uri, const char *secret, const char *cname, unsigned buffer_ms, int initiate_conn) {
  struct rist_peer_config *pc = NULL;
  struct rist_peer *peer;

  if (rist_parse_address2(peer_uri, &pc) != 0 || !pc) {
    log_line("rist: invalid peer url: %s", peer_uri);
    return -1;
  }
  pc->initiate_conn = initiate_conn;
  if (secret && secret[0]) bufcpy(pc->secret, sizeof pc->secret, secret);
  if (cname && cname[0]) bufcpy(pc->cname, sizeof pc->cname, cname);
  if (buffer_ms) {
    pc->recovery_length_min = buffer_ms;
    pc->recovery_length_max = buffer_ms;
  }
  if (rist_peer_create(ctx, &peer, pc) != 0) {
    log_line("rist: failed to add peer: %s", peer_uri);
    rist_peer_config_free2(&pc);
    return -1;
  }
  rist_peer_config_free2(&pc);
  return 0;
}

int rist_push_stats_if_due(metrics_exporter_t *mx, const char *tool_version, const metrics_entry_t *entries, size_t n, const struct rist_stats *stats) {
  if (metrics_exporter_due(mx, mono_seconds())) metrics_push_entries(mx, tool_version, entries, n);
  rist_stats_free(stats);
  return 0;
}
