/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/metrics/protocol.h"
#include "lib/signal.h"

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
    if (n < 0)
      return;
    store_ingest(store, buf, (size_t)n, now, verbose);
  }
}

int main(int argc, char **argv) {
  config_t cfg;
  args_status_t st;
  int uds_fd, http_fd;
  static store_t store; /* ~1.9MB, keep off the stack */

  log_set_color(log_color_prescan(argc, argv));
  log_line_ansi("\e[1m%s\e[0m \e[0;32mv%s\e[0m \e[0;37m%s\e[0m \e[0;37m%s\e[0m \e[0;34m%s\e[0m", TOOL_NAME, TOOL_VERSION, BUILD_ARCH, BUILD_TYPE, BUILD_LINK);
  st = args_parse(argc, argv, &cfg);
  if (st == ARGS_HELP)
    return 0;
  if (st == ARGS_ERR) {
    fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME);
    return 2;
  }
  log_set_color((log_color_t)cfg.color_mode);
  signals_install();

  uds_fd = uds_listen(cfg.sock_path);
  if (uds_fd < 0)
    return 1;
  http_fd = http_listen(cfg.family, cfg.listen_addr, cfg.listen_port);
  if (http_fd < 0) {
    close(uds_fd);
    unlink(cfg.sock_path);
    return 1;
  }

  store_init(&store);
  log_line("receiving snapshots on %s, serving http://%s:%u/metrics", cfg.sock_path, cfg.listen_addr, cfg.listen_port);

  while (!signal_stop_requested()) {
    struct pollfd pfds[2];
    int nready;
    double now;

    pfds[0].fd = uds_fd;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;
    pfds[1].fd = http_fd;
    pfds[1].events = POLLIN;
    pfds[1].revents = 0;

    nready = poll(pfds, 2, POLL_TIMEOUT_MS);
    if (signal_stop_requested())
      break;
    now = mono_seconds();

    if (nready > 0 && (pfds[0].revents & POLLIN))
      drain_uds_snapshots(uds_fd, &store, now, cfg.verbose);
    if (nready > 0 && (pfds[1].revents & POLLIN))
      http_accept_and_serve(http_fd, &store, now, cfg.verbose);

    store_reap_expired(&store, now, (double)cfg.expiry_s);
  }

  close(uds_fd);
  close(http_fd);
  unlink(cfg.sock_path);
  log_line("stopped");
  return 0;
}
