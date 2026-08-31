/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "args.h"
#include "bridge.h"
#include "lib/helper/log.h"
#include "lib/metrics/export.h"
#include "lib/helper/signal.h"
#include "version.h"

int main(int argc, char **argv) {
  config_t cfg;
  char in[1024], out[1024];
  args_status_t st;
  metrics_exporter_t mx;
  int rc;

  log_set_color(log_color_prescan(argc, argv));

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
  if (cfg.daemonize && daemon(1, 1) != 0) {
    log_line(TOOL_NAME ": daemonize failed: %s", strerror(errno));
    return 1;
  }

  endpoint_describe(&cfg.in, in, sizeof in);
  endpoint_describe(&cfg.out, out, sizeof out);
  log_line_ansi("\e[1mi:\e[0m\e[0;37m%s\e[0m \e[1mo:\e[0m\e[0;37m%s\e[0m \e[1mmode:\e[0m\e[0;37m%s\e[0m \e[1mprofile:\e[0m\e[0;37m%s\e[0m", in, out,
                config_is_sender(&cfg) ? "sender" : "receiver", cfg.profile == RIST_PROF_MAIN ? "main" : "simple");
  signals_install();

  metrics_exporter_init(&mx, METRICS_COMPONENT_RIST, cfg.metrics_id, cfg.metrics_sock, (double)cfg.metrics_interval_s);
  rc = bridge_run(&cfg, &mx);
  metrics_exporter_close(&mx);
  return rc;
}
