/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lib/sys/antidebug.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/toolmain.h"
#include "lib/metrics/export.h"
#include "lib/sys/signal.h"

#include "cli/args.h"
#include "cs378x/cs378x.h"
#include "device.h"
#include "hooks.h"
#include "version.h"

#define CAM378_METRICS_POLL_MS 200

/* banner prints before parsing: --color read early */
int main(int argc, char **argv) {
  config_t cfg;
  device_state_t *dev;
  cs378x_cfg_t srv_cfg;
  cs378x_server_t *srv;
  metrics_exporter_t mx;

  antidebug_install();
  TOOLMAIN_STARTUP(argc, argv, &cfg, args_parse);
  if (toolmain_daemonize(cfg.daemonize, TOOL_NAME)) return 1;
  dev = device_state_new(cfg.key_path, cfg.cw_len, cfg.serial, cfg.caid);
  if (!dev) {
    fprintf(stderr, "%s: cannot load RSA private key from -k %s\n", TOOL_NAME, cfg.key_path);
    return 1;
  }

  memset(&srv_cfg, 0, sizeof srv_cfg);
  srv_cfg.bind = cfg.bind;
  srv_cfg.port = cfg.port;
  srv_cfg.username = cfg.username;
  srv_cfg.password = cfg.password;
  srv_cfg.verbose = cfg.verbose;
  signals_install();
  srv = cs378x_server_start(&srv_cfg, cam378_ecm_cb, cam378_emm_cb, dev);
  if (!srv) {
    fprintf(stderr, "%s: failed to start cs378x listener on %s port %u\n", TOOL_NAME, cfg.bind, cfg.port);
    device_state_free(dev);
    return 1;
  }
  log_line(TOOL_NAME ": listening on %s port %u", cfg.bind, cfg.port);

  metrics_exporter_init(&mx, METRICS_COMPONENT_CAM378, cfg.metrics_id, cfg.metrics_sock, (double)cfg.metrics_interval_s);
  if (!metrics_exporter_enabled(&mx)) {
    while (!signal_stop_requested()) sleep_interruptible(3600.0);
  } else {
    const char *algo_name = cfg.cw_len == 8 ? "csa2" : "cissa";
    struct timespec tick = {0, CAM378_METRICS_POLL_MS * 1000000L};
    while (!signal_stop_requested()) {
      cam378_push_metrics(&mx, srv, dev, algo_name);
      nanosleep(&tick, NULL);
    }
  }
  metrics_exporter_close(&mx);
  cs378x_server_stop(srv);
  device_state_free(dev);
  return 0;
}
