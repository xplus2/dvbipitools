/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "lib/log.h"
#include "lib/signal.h"

#include "args.h"
#include "cs378x/cs378x.h"
#include "device.h"
#include "version.h"

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

  while (!signal_stop_requested())
    pause();

  cs378x_server_stop(srv);
  device_state_free(dev);
  return 0;
}
