/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/metrics/protocol.h"
#include "lib/sys/signal.h"
#include "lib/helper/toolmain.h"
#include "lib/net/tls_server.h"
#include "cli/args.h"
#include "httpserver.h"
#include "lib/metrics/store.h"
#include "version.h"

#define POLL_TIMEOUT_MS 1000
#define UDS_DRAIN_MAX 256

/* 0 = path free to bind. refuses non-sockets and live listeners */
static int sock_path_clear(const char *path) {
  struct stat sb;
  struct sockaddr_un addr;
  int fd, live;

  if (lstat(path, &sb) < 0) {
    if (errno == ENOENT) return 0;
    log_code("MS500112", "stat %s failed: %s", path, strerror(errno));
    return -1;
  }
  if (!S_ISSOCK(sb.st_mode)) {
    log_code("MS000713", "%s exists and is not a socket", path);
    return -1;
  }
  fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    log_code("MS500109", "socket() failed: %s", strerror(errno));
    return -1;
  }
  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  bufcpy(addr.sun_path, sizeof addr.sun_path, path);
  live = connect(fd, (struct sockaddr *)&addr, (socklen_t)(sizeof addr.sun_family + strlen(path) + 1)) == 0;
  if (!live && errno != ECONNREFUSED && errno != ENOENT) {
    log_code("MS500112", "probe %s failed: %s", path, strerror(errno));
    close(fd);
    return -1;
  }
  close(fd);
  if (live) {
    log_code("MS000714", "%s is in use by another process", path);
    return -1;
  }
  if (unlink(path) < 0 && errno != ENOENT) {
    log_code("MS500112", "unlink %s failed: %s", path, strerror(errno));
    return -1;
  }
  return 0;
}

static int uds_listen(const char *path) {
  int fd;
  int rc;
  mode_t old_umask;
  struct sockaddr_un addr;

  if (strlen(path) >= sizeof addr.sun_path) {
    log_code("MS000139", "socket path too long: %s", path);
    return -1;
  }
  if (sock_path_clear(path)) return -1;
  fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    log_code("MS500109", "socket() failed: %s", strerror(errno));
    return -1;
  }
  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  bufcpy(addr.sun_path, sizeof addr.sun_path, path);
  old_umask = umask(0117); /* socket mode 0660: owner and group only */
  rc = bind(fd, (struct sockaddr *)&addr, (socklen_t)(sizeof addr.sun_family + strlen(path) + 1));
  umask(old_umask);
  if (rc < 0) {
    log_code("MS500112", "bind %s failed: %s", path, strerror(errno));
    close(fd);
    return -1;
  }
  return fd;
}

/* capped: a datagram flood must not starve HTTP service */
static void drain_uds_snapshots(int uds_fd, store_t *store, double now, int verbose) {
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  for (int i = 0; i < UDS_DRAIN_MAX; i++) {
    ssize_t n = recvfrom(uds_fd, buf, sizeof buf, MSG_TRUNC, NULL, NULL); /* n = real length, store rejects oversize */
    if (n < 0) return;
    store_ingest(store, buf, (size_t)n, now, verbose);
  }
}

int main(int argc, char **argv) {
  config_t cfg;
  int uds_fd, http_fd;
  http_server_t *hs;
  tls_server_ctx_t *tls_ctx = NULL;
  static store_t store;

  TOOLMAIN_STARTUP(argc, argv, &cfg, args_parse);
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
  hs = http_server_new(http_fd, tls_ctx, cfg.http_auth);
  if (!hs) {
    close(uds_fd);
    close(http_fd);
    unlink(cfg.sock_path);
    tls_server_ctx_free(tls_ctx);
    return 1;
  }

  if (toolmain_daemonize(cfg.daemonize, TOOL_NAME)) {
    http_server_free(hs);
    close(uds_fd);
    close(http_fd);
    unlink(cfg.sock_path);
    tls_server_ctx_free(tls_ctx);
    return 1;
  }
  store_init(&store);
  log_line("receiving snapshots on %s, serving %s://%s%s%s:%u/metrics", cfg.sock_path, tls_ctx ? "https" : "http",
           cfg.family == AF_INET6 ? "[" : "", cfg.listen_addr, cfg.family == AF_INET6 ? "]" : "", cfg.listen_port);

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
      if (tls_ctx) {
        if (tls_server_ctx_reload(tls_ctx) == 0) log_line("SIGUSR1: TLS cert reload ok");
        else                                     log_code("MW300106", "SIGUSR1: TLS cert reload failed");
      } else
        log_code("MW700105", "SIGUSR1: no TLS configured, ignoring");
    }
  }

  http_server_free(hs);
  close(uds_fd);
  close(http_fd);
  unlink(cfg.sock_path);
  store_free(&store);
  tls_server_ctx_free(tls_ctx);
  log_line("stopped");
  return 0;
}
