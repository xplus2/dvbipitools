/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "lib/log.h"
#include "lib/signal.h"

#include "args.h"
#include "capture.h"
#include "channel.h"
#include "listen.h"
#include "mcsend.h"
#include "ret.h"
#include "version.h"

#define MC_SEND_TTL 1 /* fixed, no CLI flag for this - matches dipitvhead's own default */

typedef struct {
  mcsend_table_t *mt;
} send_ctx_t;

static void send_mc_impl(const channel_t *c, const unsigned char *pkt, size_t len, int dscp, void *user) {
  send_ctx_t *ctx = (send_ctx_t *)user;
  mcast_t *m;
  (void)c;
  if (!ctx->mt)
    return; /* --no-mc-ret: no MC session to repair via, the unicast reply path still covers every client */
  m = mcsend_get(ctx->mt, (channel_t *)c);
  if (!m)
    return; /* socket not provisioned yet - a NACK can race ahead of capture's first packet for a new channel */
  mcast_set_tos(m, dscp);
  mcast_send(m, pkt, len);
}

static void send_unicast_impl(int fd, const struct sockaddr *to, socklen_t tolen, const unsigned char *pkt, size_t len, int dscp, void *user) {
  (void)user;
  if (to->sa_family == AF_INET6)
    setsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &dscp, sizeof dscp);
  else
    setsockopt(fd, IPPROTO_IP, IP_TOS, &dscp, sizeof dscp);
  if (sendto(fd, pkt, len, 0, to, tolen) < 0)
    log_line("dipiret: unicast reply sendto: %s", strerror(errno));
}

static log_color_t color_prescan(int argc, char **argv) {
  int i;
  for (i = 1; i < argc; i++) {
    const char *v = NULL;
    if (!strcmp(argv[i], "--color") && i + 1 < argc)
      v = argv[i + 1];
    else if (!strncmp(argv[i], "--color=", 8))
      v = argv[i] + 8;
    if (!v)
      continue;
    if (!strcmp(v, "always"))
      return LOG_COLOR_ALWAYS;
    if (!strcmp(v, "never"))
      return LOG_COLOR_NEVER;
  }
  return LOG_COLOR_AUTO;
}

int main(int argc, char **argv) {
  config_t cfg;
  args_status_t st;
  size_t max_channels;
  channel_table_t *channels;
  mcsend_table_t *mt = NULL;
  capture_t *cap;
  char errbuf[256];
  send_ctx_t send_ctx;
  ret_ctx_t *r;
  listen_pool_t *pool;

  log_set_color(color_prescan(argc, argv));
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
  channels = channel_table_new(cfg.buffer_ms, max_channels); /* ring_slots ~= buffer_ms: ~1 packet/ms assumption, documented in the plan */
  if (!channels) {
    fprintf(stderr, "%s: out of memory allocating channel table\n", TOOL_NAME);
    return 1;
  }

  if (!cfg.no_mc_ret) {
    mt = mcsend_table_new(max_channels, cfg.iface, MC_SEND_TTL);
    if (!mt) {
      fprintf(stderr, "%s: out of memory allocating MC RET session table\n", TOOL_NAME);
      return 1;
    }
  }
  cap = capture_open(cfg.iface, cfg.bpf_expr, cfg.range_ptrs, cfg.range_count, errbuf, sizeof errbuf);
  if (!cap) {
    fprintf(stderr, "%s: %s\n", TOOL_NAME, errbuf);
    return 1;
  }
  if (capture_drop_privileges(cfg.user) != 0) {
    fprintf(stderr, "%s: failed to drop privileges to -u %s\n", TOOL_NAME, cfg.user);
    return 1;
  }
  send_ctx.mt = mt;
  r = ret_ctx_new(channels, cfg.rtx_pt, send_mc_impl, send_unicast_impl, &send_ctx);
  if (!r) {
    fprintf(stderr, "%s: out of memory creating ret context\n", TOOL_NAME);
    return 1;
  }
  pool = listen_pool_start(cfg.listen_family, cfg.listen_addr, cfg.listen_port, cfg.workers, r);
  if (!pool) {
    fprintf(stderr, "%s: failed to start listen workers on %s:%u\n", TOOL_NAME, cfg.listen_addr, cfg.listen_port);
    return 1;
  }

  signals_install();
  log_line("dipiret: capturing, %u worker(s) on %s:%u, %zu channel slots%s", cfg.workers, cfg.listen_addr, cfg.listen_port, max_channels, mt ? "" : " (--no-mc-ret)");
  capture_run(cap, channels, mt, cfg.ff_port);
  listen_pool_stop(pool);
  ret_ctx_free(r);
  capture_close(cap);
  if (mt)
    mcsend_table_free(mt);
  channel_table_free(channels);
  return 0;
}
