/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_RIST_RISTPEER_H
#define DVBIPITOOLS_LIB_NET_RIST_RISTPEER_H

#include <stddef.h>

#include "lib/metrics/export.h"

struct rist_ctx;
struct rist_stats;

/* 0 ok, -1 rist_parse_address2/rist_peer_create failed (logged) */
int rist_add_peer(struct rist_ctx *ctx, const char *peer_uri, const char *secret, const char *cname, unsigned buffer_ms, int initiate_conn);

/* pushes entries if metrics_exporter_due(), then rist_stats_free(stats). always returns 0 */
int rist_push_stats_if_due(metrics_exporter_t *mx, const char *tool_version, const metrics_entry_t *entries, size_t n, const struct rist_stats *stats);

#endif
