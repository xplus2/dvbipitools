/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <unistd.h>

#include "announce.h"
#include "args.h"
#include "lib/helper/log.h"
#include "lib/helper/toolmain.h"
#include "lib/metrics/export.h"
#include "lib/helper/signal.h"
#include "listen.h"
#include "version.h"

int main(int argc, char **argv) {
  config_t cfg;
  args_status_t st;
  metrics_exporter_t mx;
  int rc;

  log_set_color(log_color_prescan(argc, argv));
  toolmain_print_banner(TOOL_NAME, TOOL_VERSION, BUILD_ARCH, BUILD_TYPE, BUILD_LINK);
  st = args_parse(argc, argv, &cfg);
  if (st == ARGS_OK) log_set_color((log_color_t)cfg.color_mode);
  if (st == ARGS_HELP) return 0;
  if (st == ARGS_ERR) {
    fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME);
    return 2;
  }
  if (toolmain_daemonize(cfg.daemonize, TOOL_NAME)) return 1;
  signals_install();
  if (cfg.mode != MODE_ANNOUNCE) return listen_run(&cfg);
  metrics_exporter_init(&mx, METRICS_COMPONENT_BCG, cfg.metrics_id, cfg.metrics_sock, (double)cfg.metrics_interval_s);
  rc = announce_run(&cfg, &mx);
  metrics_exporter_close(&mx);
  return rc;
}
