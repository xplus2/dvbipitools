/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/metrics/protocol.h"
#include "lib/helper/signal.h"
#include "lib/helper/toolmain.h"
#include "lib/net/tls_server.h"

#include "args.h"
#include "httpserver.h"
#include "store.h"
#include "version.h"

#define POLL_TIMEOUT_MS 1000

static int uds_listen(const char *path) {
  int fd;
  struct sockaddr_un addr;

  if (strlen(path) >= sizeof addr.sun_path) {
    log_line("dipimetrics: socket path too long: %s", path);
    return -1;
  }
  fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    log_line("dipimetrics: socket() failed: %s", strerror(errno));
    return -1;
  }
  unlink(path);
  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  bufcpy(addr.sun_path, sizeof addr.sun_path, path);
  if (bind(fd, (struct sockaddr *)&addr, (socklen_t)(sizeof addr.sun_family + strlen(path) + 1)) < 0) {
    log_line("dipimetrics: bind %s failed: %s", path, strerror(errno));
    close(fd);
    return -1;
  }
  return fd;
}

static void drain_uds_snapshots(int uds_fd, store_t *store, double now, int verbose) {
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  for (;;) {
    ssize_t n = recvfrom(uds_fd, buf, sizeof buf, 0, NULL, NULL);
    if (n < 0) return;
    store_ingest(store, buf, (size_t)n, now, verbose);
  }
}

int main(int argc, char **argv) {
  config_t cfg;
  int uds_fd, http_fd;
  http_server_t *hs;
  tls_server_ctx_t *tls_ctx = NULL;
  static store_t store; /* ~1.9MB, keeps off stack */

  TOOLMAIN_STARTUP(argc, argv, &cfg, args_parse);
  if (toolmain_daemonize(cfg.daemonize, TOOL_NAME)) return 1;
  signals_install();
  if (cfg.tls_cert) {
    tls_ctx = tls_server_ctx_new(cfg.tls_cert, cfg.tls_key);
    if (!tls_ctx) return 1;
  }

  uds_fd = uds_listen(cfg.sock_path);
  if (uds_fd < 0) {
    tls_server_ctx_free(tls_ctx);
    return 1;
  }
  http_fd = http_listen(cfg.family, cfg.listen_addr, cfg.listen_port);
  if (http_fd < 0) {
    close(uds_fd);
    unlink(cfg.sock_path);
    tls_server_ctx_free(tls_ctx);
    return 1;
  }
  hs = http_server_new(http_fd, tls_ctx);
  if (!hs) {
    close(uds_fd);
    close(http_fd);
    unlink(cfg.sock_path);
    tls_server_ctx_free(tls_ctx);
    return 1;
  }

  store_init(&store);
  log_line("receiving snapshots on %s, serving %s://%s:%u/metrics", cfg.sock_path, tls_ctx ? "https" : "http", cfg.listen_addr, cfg.listen_port);

  while (!signal_stop_requested()) {
    struct pollfd pfds[1 + HTTP_MAX_CONNS + 1]; /* uds + listen + open connections */
    int n = 0;
    double now;

    pfds[n].fd = uds_fd;
    pfds[n].events = POLLIN;
    pfds[n].revents = 0;
    n++;
    http_server_poll_fds(hs, pfds, sizeof pfds / sizeof *pfds, &n);

    poll(pfds, (nfds_t)n, POLL_TIMEOUT_MS);
    if (signal_stop_requested()) break;
    now = mono_seconds();

    if (pfds[0].revents & POLLIN) drain_uds_snapshots(uds_fd, &store, now, cfg.verbose);
    http_server_service(hs, pfds, n, &store, now, cfg.verbose);
    store_reap_expired(&store, now, (double)cfg.expiry_s);

    if (signal_tls_reload_requested()) {
      if (tls_ctx)
        log_line("dipimetrics: SIGUSR1: TLS cert reload %s", tls_server_ctx_reload(tls_ctx) == 0 ? "ok" : "failed");
      else
        log_line("dipimetrics: SIGUSR1: no TLS configured, ignoring");
    }
  }

  http_server_free(hs);
  close(uds_fd);
  close(http_fd);
  unlink(cfg.sock_path);
  tls_server_ctx_free(tls_ctx);
  log_line("stopped");
  return 0;
}
