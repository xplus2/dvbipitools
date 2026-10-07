/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_RIST_RISTPEER_H
#define DVBIPITOOLS_LIB_NET_RIST_RISTPEER_H

#include <stddef.h>

#include "lib/metrics/export.h"

struct rist_ctx;
struct rist_stats;

/* listening peer: data port (and rtcp_port: +1 sibling, simple profile) must be free.
   librist crash in rist_peer_create() on taken rtcp_port. 1 ok, 0 taken */
int rist_listen_ports_usable(const char *uri, int rtcp_port);

/* key_size 128/256, 0 = URI/lib default. rtcp_port: simple profile, listening
   0 ok, -1 rist_parse_address2/rist_peer_create failed (logged) */
int rist_add_peer(struct rist_ctx *ctx, const char *peer_uri, const char *secret, int key_size, const char *cname, unsigned buffer_ms, int initiate_conn, int rtcp_port);

/* pushes entries if metrics_exporter_due(), then rist_stats_free(stats). always returns 0 */
void rist_push_stats_if_due(metrics_exporter_t *mx, const char *tool_version, const metrics_entry_t *entries, size_t n, const struct rist_stats *stats);

#endif
