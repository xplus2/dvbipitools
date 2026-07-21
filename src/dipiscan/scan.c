/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <string.h>
#include <time.h>

#include "format.h"
#include "lib/demux/psi.h"
#include "lib/demux/rtp.h"
#include "lib/demux/tspack.h"
#include "lib/log.h"
#include "lib/net/multicast.h"
#include "lib/net/udpxy.h"
#include "lib/signal.h"
#include "scan.h"
#include "version.h"

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* candidate group address at sweep index i (1..254): base with last byte/octet replaced */
static void addr_at(const config_t *cfg, unsigned i, char *buf, size_t n) {
  unsigned char a[16];
  size_t alen = (cfg->family == AF_INET6) ? 16 : 4;
  memcpy(a, cfg->base, alen);
  a[alen - 1] = (unsigned char)i;
  inet_ntop(cfg->family, a, buf, (socklen_t)n);
}

typedef enum { PROBE_NONE, PROBE_UNNAMED, PROBE_NAMED } probe_kind_t;

typedef struct {
  probe_kind_t kind;
  int rtp_wrapped; /* -1 unknown (no data), 0 udp, 1 rtp */
  char name[PSI_NAME];
  unsigned pkts;
  unsigned tsid, onid, sid;
} probe_result_t;

typedef struct {
  psi_t *psi;
  unsigned pkts;
} probe_ctx_t;

static int probe_cb(void *v, const unsigned char *pkt) {
  probe_ctx_t *pc = v;
  pc->pkts++;
  psi_feed(pc->psi, pkt);
  return (psi_have_pat(pc->psi) && psi_service_name(pc->psi)[0]) ? 1 : 0;
}

typedef ssize_t (*chan_read_fn)(void *ctx, unsigned char *buf, size_t cap);

static ssize_t mcast_read_adapter(void *ctx, unsigned char *buf, size_t cap) { return mcast_recv((mcast_t *)ctx, buf, cap); }
static ssize_t udpxy_read_adapter(void *ctx, unsigned char *buf, size_t cap) { return udpxy_read((udpxy_t *)ctx, buf, cap); }

/* budget until first packet, dead addrs bail early */
#define PROBE_QUIET_MS 300

/* read until named, timeout, or interrupted */
static void probe_common(chan_read_fn rf, void *rctx, int timeout_ms, probe_result_t *r) {
  unsigned char buf[65536];
  tspack_t pz;
  probe_ctx_t pc;
  double deadline, quiet_deadline;

  memset(&pz, 0, sizeof pz);
  memset(r, 0, sizeof *r);
  r->rtp_wrapped = -1;
  pc.psi = psi_new();
  pc.pkts = 0;
  deadline = mono() + (double)timeout_ms / 1000.0;
  quiet_deadline = mono() + (double)PROBE_QUIET_MS / 1000.0;
  if (quiet_deadline > deadline)
    quiet_deadline = deadline;
  while (mono() < (pc.pkts ? deadline : quiet_deadline) && !signal_stop_requested()) {
    ssize_t n = rf(rctx, buf, sizeof buf);
    size_t off;
    if (n < 0)
      break;
    if (n == 0)
      continue;
    off = rtp_payload_offset(buf, (size_t)n);
    if (r->rtp_wrapped < 0)
      r->rtp_wrapped = off ? 1 : 0;
    if (off) {
      memmove(buf, buf + off, (size_t)n - off);
      n -= (ssize_t)off;
    }
    if (tspack_feed(&pz, buf, (size_t)n, probe_cb, &pc))
      break;
  }
  r->pkts = pc.pkts;
  if (pc.pkts == 0) {
    r->kind = PROBE_NONE;
  } else if (psi_have_pat(pc.psi) && psi_service_name(pc.psi)[0]) {
    r->kind = PROBE_NAMED;
    strncpy(r->name, psi_service_name(pc.psi), sizeof r->name - 1);
    r->name[sizeof r->name - 1] = '\0';
    r->tsid = psi_transport_stream_id(pc.psi);
    r->onid = psi_original_network_id(pc.psi);
    r->sid = psi_program_number(pc.psi);
  } else {
    r->kind = PROBE_UNNAMED;
    if (psi_have_pat(pc.psi)) {
      r->tsid = psi_transport_stream_id(pc.psi);
      r->sid = psi_program_number(pc.psi);
    }
  }
  psi_free(pc.psi);
}

static void probe_address(const config_t *cfg, const char *group, unsigned port, probe_result_t *r) {
  if (cfg->udpxy) {
    char path[300];
    udpxy_t *u;
    snprintf(path, sizeof path, "/udp/%s:%u/", group, port);
    u = udpxy_open(cfg->udpxy_host, cfg->udpxy_port, path, TOOL_NAME "/" TOOL_VERSION);
    if (!u) {
      memset(r, 0, sizeof *r);
      r->kind = PROBE_NONE;
      return;
    }
    probe_common(udpxy_read_adapter, u, cfg->timeout_ms, r);
    udpxy_close(u);
  } else {
    mcast_t *m = mcast_open(cfg->family, group, port, cfg->iface, 200);
    if (!m) {
      memset(r, 0, sizeof *r);
      r->kind = PROBE_NONE;
      return;
    }
    probe_common(mcast_read_adapter, m, cfg->timeout_ms, r);
    mcast_close(m);
  }
}

int scan_run(const config_t *cfg, FILE *out) {
  char invocation[256], basestr[64];
  unsigned i, port, total = 0, found = 0;
  double start = mono();
  int interrupted = 0;

  args_base_describe(cfg, basestr, sizeof basestr);
  if (cfg->port_lo == cfg->port_hi)
    snprintf(invocation, sizeof invocation, "%s --mcast %s --port %u --timeout %d", TOOL_NAME, basestr, cfg->port_lo, cfg->timeout_ms / 1000);
  else
    snprintf(invocation, sizeof invocation, "%s --mcast %s --port %u-%u --timeout %d", TOOL_NAME, basestr, cfg->port_lo, cfg->port_hi, cfg->timeout_ms / 1000);
  format_init(out, cfg->format, invocation);
  for (i = 1; i < 255 && !interrupted; i++) {
    char group[64];
    addr_at(cfg, i, group, sizeof group);
    for (port = cfg->port_lo; port <= cfg->port_hi; port++) {
      probe_result_t r;
      const char *proto, *name;
      char uri[96];

      total++;
      probe_address(cfg, group, port, &r);
      proto = (r.rtp_wrapped == 1) ? "rtp" : "udp";
      if (cfg->family == AF_INET6)
        snprintf(uri, sizeof uri, "%s://@[%s]:%u", proto, group, port);
      else
        snprintf(uri, sizeof uri, "%s://@%s:%u", proto, group, port);

      if (r.kind == PROBE_NONE) {
        log_line_ansi("%3u/254 %-28s \e[0;31mno stream\e[0m", i, uri);
      } else {
        name = (r.kind == PROBE_NAMED) ? r.name : "(no SDT)";
        found++;
        if (cfg->verbose)
          log_line("%3u/254 %-28s %-32s [%u pkts]", i, uri, name, r.pkts);
        else
          log_line("%3u/254 %-28s %s", i, uri, name);
        format_item(out, cfg->format, name, uri, r.tsid, r.onid, r.sid);
      }
      if (signal_stop_requested()) {
        interrupted = 1;
        break;
      }
    }
  }
  format_close(out, cfg->format);
  if (interrupted)
    log_line("interrupted: found %u station%s (of %u probed) in %.1fs", found, found == 1 ? "" : "s", total, mono() - start);
  else
    log_line("found %u station%s in %.1fs", found, found == 1 ? "" : "s", mono() - start);
  return interrupted;
}
