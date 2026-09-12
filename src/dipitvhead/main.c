/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>

#include "args.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/metrics/export.h"
#include "lib/helper/signal.h"
#include "lib/helper/toolmain.h"
#include "tvhead/tvhead.h"
#include "version.h"

/* banner prints before parsing: --color read early */
int main(int argc, char **argv) {
  config_t cfg;
  char src[600], mcast[80];
  metrics_exporter_t mx;
  int rc;

  TOOLMAIN_STARTUP(argc, argv, &cfg, args_parse);
  if (toolmain_daemonize(cfg.daemonize, TOOL_NAME)) return 1;
  if (cfg.mcast_port)
    mcast_describe(&cfg, mcast, sizeof mcast);
  else
    bufcpy(mcast, sizeof mcast, "-");
  if (cfg.n_inputs == 1) {
    source_describe(&cfg.inputs[0].input, src, sizeof src);
    log_line_ansi("\e[1mi:\e[0m\e[0;37m%s\e[0m \e[1mm:\e[0m\e[0;37m%s\e[0m \e[1mrtp:\e[0m\e[0;37m%s\e[0m", src, mcast, cfg.rtp ? "yes" : "no");
  } else {
    log_line_ansi("\e[1minputs:\e[0m\e[0;37m%u\e[0m \e[1mm:\e[0m\e[0;37m%s\e[0m \e[1mrtp:\e[0m\e[0;37m%s\e[0m", cfg.n_inputs, mcast, cfg.rtp ? "yes" : "no");
  }

  signals_install();
  metrics_exporter_init(&mx, METRICS_COMPONENT_TVHEAD, cfg.metrics_id, cfg.metrics_sock, (double)cfg.metrics_interval_s);
  rc = tvhead_run(&cfg, &mx);
  metrics_exporter_close(&mx);
  return rc;
}
