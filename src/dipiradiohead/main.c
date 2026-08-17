/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "args.h"
#include "lib/log.h"
#include "lib/metrics/export.h"
#include "lib/signal.h"
#include "radiohead/radiohead.h"
#include "version.h"

/* banner prints before parsing: --color read early */
int main(int argc, char **argv) {
  config_t cfg;
  char mcast[80];
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
  if (cfg.mcast_port)
    mcast_describe(&cfg, mcast, sizeof mcast);
  else
    snprintf(mcast, sizeof mcast, "-");
  if (cfg.n_inputs == 1) {
    log_line_ansi("\e[1mi:\e[0m\e[0;37m%s\e[0m \e[1mm:\e[0m\e[0;37m%s\e[0m \e[1mrtp:\e[0m\e[0;37m%s\e[0m", cfg.inputs[0].uri, mcast, cfg.rtp ? "yes" : "no");
  } else {
    log_line_ansi("\e[1minputs:\e[0m\e[0;37m%u\e[0m \e[1mm:\e[0m\e[0;37m%s\e[0m \e[1mrtp:\e[0m\e[0;37m%s\e[0m", cfg.n_inputs, mcast, cfg.rtp ? "yes" : "no");
  }
  signals_install();
  metrics_exporter_init(&mx, METRICS_COMPONENT_RADIOHEAD, cfg.metrics_id, cfg.metrics_sock, (double)cfg.metrics_interval_s);
  rc = radiohead_run(&cfg, &mx);
  metrics_exporter_close(&mx);
  return rc;
}
