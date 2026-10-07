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
#include "lib/sys/signal.h"

#include "ristpeer.h"

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
  return *port > 0 && *port < 65535;
}

int rist_listen_ports_usable(const char *uri, int rtcp_port) {
  char host[128];
  unsigned port;

  if (!split_listen_uri(uri, host, sizeof host, &port)) return 1; /* let librist report it */
  if (!udp_port_bindable(host, port)) {
    log_line("rist: UDP port %u already in use", port);
    return 0;
  }
  if (rtcp_port && !udp_port_bindable(host, port + 1u)) {
    log_line("rist: UDP port %u (RTCP of %u) already in use", port + 1u, port);
    return 0;
  }
  return 1;
}

int rist_add_peer(struct rist_ctx *ctx, const char *peer_uri, const char *secret, int key_size, const char *cname, unsigned buffer_ms, int initiate_conn, int rtcp_port) {
  struct rist_peer_config *pc = NULL;
  struct rist_peer *peer;

  if (rist_parse_address2(peer_uri, &pc) != 0 || !pc) {
    log_line("rist: invalid peer url: %s", peer_uri);
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
  if (!initiate_conn && !rist_listen_ports_usable(peer_uri, rtcp_port)) {
    rist_peer_config_free2(&pc);
    return -1;
  }
  if (rist_peer_create(ctx, &peer, pc) != 0) {
    log_line("rist: failed to add peer: %s", peer_uri);
    rist_peer_config_free2(&pc);
    return -1;
  }
  rist_peer_config_free2(&pc);
  return 0;
}

void rist_push_stats_if_due(metrics_exporter_t *mx, const char *tool_version, const metrics_entry_t *entries, size_t n, const struct rist_stats *stats) {
  if (metrics_exporter_due(mx, mono_seconds())) metrics_push_entries(mx, tool_version, entries, n);
  rist_stats_free(stats);
}
