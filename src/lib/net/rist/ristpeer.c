/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <librist/librist.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/uriparse.h"
#include "lib/sys/signal.h"

#include "ristlog.h"
#include "ristpeer.h"

#define RIST_DEFAULT_BUFFER_MS 1000 /* librist recovery length default */

static int udp_port_bindable(const char *host, unsigned port) {
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  char svc[8];
  int ok = 0;

  memset(&hints, 0, sizeof hints);
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
  snprintf(svc, sizeof svc, "%u", port);
  if (getaddrinfo(host[0] ? host : NULL, svc, &hints, &res) != 0) return 1; /* let librist report */
  for (const struct addrinfo *a = res; a && !ok; a = a->ai_next) {
    int fd = socket(a->ai_family, a->ai_socktype, 0);
    if (fd < 0) continue;
    ok = bind(fd, a->ai_addr, a->ai_addrlen) == 0;
    close(fd);
  }
  freeaddrinfo(res);
  return ok;
}

/* rist://[@]host:port[?query]. host may be [ipv6]. 0 on bogus */
static int split_listen_uri(const char *uri, char *host, size_t host_cap, unsigned *port) {
  const char *p = strstr(uri, "://");
  const char *end;
  const char *colon;
  size_t n;

  if (!p) return 0;
  p += 3;
  if (*p == '@') p++;
  end = p + strcspn(p, "?/");
  colon = NULL;
  for (const char *q = p; q < end; q++)
    if (*q == ':') colon = q;
  if (!colon || colon == p) return 0;
  n = (size_t)(colon - p);
  if (*p == '[' && n >= 2 && p[n - 1] == ']') {
    p++;
    n -= 2;
  }
  if (n >= host_cap) return 0;
  memcpy(host, p, n);
  host[n] = '\0';
  *port = (unsigned)strtoul(colon + 1, NULL, 10);
  return *port > 0 && *port <= 65535;
}

int ristpeer_listen_ports_usable(const char *uri, int rtcp_port) {
  char host[128];
  unsigned port;

  if (!split_listen_uri(uri, host, sizeof host, &port)) return 1; /* let librist report it */
  if (!udp_port_bindable(host, port)) {
    log_code("LE500156", "rist: UDP port %u already in use", port);
    return 0;
  }
  if (rtcp_port && port < 65535 && !udp_port_bindable(host, port + 1u)) {
    log_code("LE500157", "rist: UDP port %u (RTCP of %u) already in use", port + 1u, port);
    return 0;
  }
  return 1;
}

int ristpeer_add(struct rist_ctx *ctx, const char *peer_uri, const char *secret, int key_size, const char *cname, unsigned buffer_ms, int initiate_conn, int rtcp_port) {
  struct rist_peer_config *pc = NULL;
  struct rist_peer *peer;

  if (rist_parse_address2(peer_uri, &pc) != 0 || !pc) {
    char *safe = uriparse_redact_dup(peer_uri);
    log_code("LE000140", "rist: invalid peer url: %s", safe ? safe : "?");
    free(safe);
    if (pc) rist_peer_config_free2(&pc);
    return -1;
  }
  pc->initiate_conn = initiate_conn;
  if (secret && secret[0]) bufcpy(pc->secret, sizeof pc->secret, secret);
  if (key_size) pc->key_size = key_size;
  if (cname && cname[0]) bufcpy(pc->cname, sizeof pc->cname, cname);
  if (buffer_ms) {
    pc->recovery_length_min = buffer_ms;
    pc->recovery_length_max = buffer_ms;
  }
  if (!initiate_conn && !ristpeer_listen_ports_usable(peer_uri, rtcp_port)) {
    rist_peer_config_free2(&pc);
    return -1;
  }
  if (rist_peer_create(ctx, &peer, pc) != 0) {
    char *safe = uriparse_redact_dup(peer_uri);
    log_code("LE600102", "rist: failed to add peer: %s", safe ? safe : "?");
    free(safe);
    rist_peer_config_free2(&pc);
    return -1;
  }
  rist_peer_config_free2(&pc);
  return 0;
}

struct rist_ctx *rist_receiver_open(const rist_recv_cfg_t *cfg) {
  struct rist_ctx *ctx;
  enum rist_profile profile = cfg->simple ? RIST_PROFILE_SIMPLE : RIST_PROFILE_MAIN;

  if (rist_receiver_create(&ctx, profile, ristlog_get(cfg->verbose)) != 0) {
    log_code("LE500113", "rist: receiver create failed");
    return NULL;
  }
  for (unsigned i = 0; i < cfg->n; i++) {
    if (ristpeer_add(ctx, cfg->uris[i], cfg->secret, cfg->key_size, cfg->cname, cfg->buffer_ms, 0, cfg->simple)) {
      rist_destroy(ctx);
      return NULL;
    }
  }
  if (rist_start(ctx) != 0) {
    log_code("LE500241", "rist: receiver start failed");
    rist_destroy(ctx);
    return NULL;
  }
  if (cfg->stats_cb) rist_stats_callback_set(ctx, RIST_STATS_INTERVAL_MS, cfg->stats_cb, cfg->stats_arg);
  return ctx;
}

void ristpeer_flow_watch_init(ristpeer_flow_watch_t *w, unsigned buffer_ms) {
  w->idle_ms = 0;
  w->reset_ms = 2 * (buffer_ms ? buffer_ms : RIST_DEFAULT_BUFFER_MS) + RIST_READ_TIMEOUT_MS;
  w->got = 0;
  w->flow_id = 0;
  w->last = 0.0;
}

int ristpeer_flow_watch_data(ristpeer_flow_watch_t *w, uint32_t flow_id) {
  double now = mono_seconds();

  if (w->got && flow_id != w->flow_id && now - w->last < w->reset_ms / 1000.0) return 0;
  w->flow_id = flow_id;
  w->last = now;
  w->idle_ms = 0;
  w->got = 1;
  return 1;
}

int ristpeer_flow_watch_idle(ristpeer_flow_watch_t *w) {
  w->idle_ms += RIST_READ_TIMEOUT_MS;
  if (!w->got || w->idle_ms < w->reset_ms) return 0;
  w->idle_ms = 0;
  w->got = 0;
  return 1;
}

size_t ristpeer_receiver_flow_entries(const struct rist_stats *stats, metrics_entry_t *out) {
  const struct rist_stats_receiver_flow *f = &stats->stats.receiver_flow;
  size_t n = 0;

  if (stats->stats_type != RIST_STATS_RECEIVER_FLOW) return 0;
  out[n++] = (metrics_entry_t){METRICS_ID_RIST_RECEIVER_RECEIVED_TOTAL, NULL, f->received};
  out[n++] = (metrics_entry_t){METRICS_ID_RIST_RECEIVER_MISSING_TOTAL, NULL, f->missing};
  out[n++] = (metrics_entry_t){METRICS_ID_RIST_RECEIVER_RECOVERED_TOTAL, NULL, f->recovered};
  out[n++] = (metrics_entry_t){METRICS_ID_RIST_RECEIVER_LOST_TOTAL, NULL, f->lost};
  out[n++] = (metrics_entry_t){METRICS_ID_RIST_RECEIVER_RTT_MILLISECONDS, NULL, f->rtt};
#ifdef DVBIPITOOLS_RIST_HAVE_AVG_BUFFER_TIME
  out[n++] = (metrics_entry_t){METRICS_ID_RIST_RECEIVER_BUFFER_MILLISECONDS, NULL, f->avg_buffer_time};
#endif
  return n;
}

void ristpeer_push_stats_if_due(metrics_exporter_t *mx, const char *tool_version, const metrics_entry_t *entries, size_t n, const struct rist_stats *stats) {
  if (metrics_exporter_due(mx, mono_seconds())) metrics_push_entries(mx, tool_version, entries, n);
  rist_stats_free(stats);
}
