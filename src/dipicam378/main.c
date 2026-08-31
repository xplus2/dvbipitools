/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/metrics/export.h"
#include "lib/helper/signal.h"

#include "args.h"
#include "cs378x/cs378x.h"
#include "device.h"
#include "version.h"

#define CAM378_METRICS_POLL_MS 200

static void push_metrics(metrics_exporter_t *mx, cs378x_server_t *srv, const device_state_t *dev, const char *algo_name) {
  cs378x_metrics_t m;
  metrics_writer_t w;

  if (!metrics_exporter_due(mx, mono_seconds()) || metrics_exporter_begin(mx, &w, TOOL_VERSION))
    return;
  cs378x_server_get_metrics(srv, &m);
  metrics_writer_put(&w, METRICS_ID_CAM_CONNECTIONS_ACTIVE, NULL, m.connections_active);
  metrics_writer_put(&w, METRICS_ID_CAM_CONNECTIONS_TOTAL, NULL, m.connections_total);
  for (int i = 0; i < CAM_AUTH_REASON_COUNT; i++)
    if (m.auth_errors_total[i])
      metrics_writer_put(&w, METRICS_ID_CAM_AUTH_ERRORS_TOTAL, cs378x_auth_reason_name((cam_auth_reason_t)i), m.auth_errors_total[i]);
  metrics_writer_put(&w, METRICS_ID_CAM_SERVICES_ACTIVE, NULL, device_state_services_active(dev));
  metrics_writer_put(&w, METRICS_ID_CAS_ECM_TOTAL, algo_name, m.ecm_total);
  metrics_writer_put(&w, METRICS_ID_CAS_ECM_ERRORS_TOTAL, algo_name, m.ecm_errors_total);
  metrics_writer_put(&w, METRICS_ID_CAS_EMM_TOTAL, algo_name, m.emm_total);
  metrics_exporter_send(mx, &w);
}

/* banner prints before parsing: --color read early */
static int ecm_cb(const unsigned char *ecm, size_t ecm_len, unsigned srvid, unsigned caid, unsigned prid, unsigned char cw_out[16], void *user) {
  (void)prid;
  return device_resolve_cw((device_state_t *)user, ecm, ecm_len, srvid, caid, cw_out);
}

static void emm_cb(const unsigned char *emm, size_t emm_len, unsigned caid, unsigned provid, void *user) {
  (void)caid;
  (void)provid;
  device_on_emm((device_state_t *)user, emm, emm_len);
}

int main(int argc, char **argv) {
  config_t cfg;
  args_status_t st;
  device_state_t *dev;
  cs378x_cfg_t srv_cfg;
  cs378x_server_t *srv;
  metrics_exporter_t mx;

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

  dev = device_state_new(cfg.key_path, cfg.cw_len, cfg.serial, cfg.caid);
  if (!dev) {
    fprintf(stderr, "%s: cannot load RSA private key from -k %s\n", TOOL_NAME, cfg.key_path);
    return 1;
  }

  srv_cfg.port = cfg.port;
  srv_cfg.username = cfg.username;
  srv_cfg.password = cfg.password;
  srv_cfg.verbose = cfg.verbose;

  signals_install();
  srv = cs378x_server_start(&srv_cfg, ecm_cb, emm_cb, dev);
  if (!srv) {
    fprintf(stderr, "%s: failed to start cs378x listener on port %u\n", TOOL_NAME, cfg.port);
    device_state_free(dev);
    return 1;
  }
  log_line(TOOL_NAME ": listening on port %u", cfg.port);

  metrics_exporter_init(&mx, METRICS_COMPONENT_CAM378, cfg.metrics_id, cfg.metrics_sock, (double)cfg.metrics_interval_s);
  if (!metrics_exporter_enabled(&mx)) {
    while (!signal_stop_requested())
      pause();
  } else {
    const char *algo_name = cfg.cw_len == 8 ? "csa2" : "cissa";
    struct timespec tick = {0, CAM378_METRICS_POLL_MS * 1000000L};
    while (!signal_stop_requested()) {
      push_metrics(&mx, srv, dev, algo_name);
      nanosleep(&tick, NULL);
    }
  }
  metrics_exporter_close(&mx);

  cs378x_server_stop(srv);
  device_state_free(dev);
  return 0;
}
