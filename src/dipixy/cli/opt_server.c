/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <stdlib.h>

#include "lib/helper/argutil.h"
#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const yamlcfg_opt_t opts[] = {
    {'l', "listen", "YS000644", "invalid -l/--listen address: %s", 0},
    {'L', "listen-tls", "YS000645", "invalid -L/--listen-tls address: %s", 0},
    {'j', "workers", "YS000646", "invalid -j/--workers: %s (-1/-2/-3, or a positive count)", 0},
    {OPT_CPU_AFFINITY, "cpu-affinity", "YS000455", "invalid --cpu-affinity: %s (off, auto, or list like 2-5,8)", 0},
    {OPT_MAX_CLIENTS, "max-clients", "YS000647", "invalid --max-clients: %s (1..65536)", 0},
    {OPT_MAX_CHANNELS, "max-channels", "YS000648", "invalid --max-channels: %s (1..1024)", 0},
    {OPT_MAX_VARIANTS, "max-variants", "YS000659", "invalid --max-variants: %s (1..1024)", 0},
    {OPT_IDLE_TIMEOUT, "idle-timeout", "YS000649", "invalid --idle-timeout: %s (seconds, 0..86400, 0 = off)", 0},
    {OPT_TS_STARTUP_TIMEOUT, "ts.startup-timeout", "YS000651", "invalid --ts-startup-timeout: %s (seconds, 0=off)", 0},
};

args_status_t dixy_opt_server(dixy_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), dixy_cfg_keys, dixy_cfg_nkeys, cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1:
      args_free(cfg);
      return ARGS_ERR;
    default: break;
  }
  switch (c) {
    case 'I':
      cfg->iface = optarg;
      break;
    case OPT_TLS_CERT:
      cfg->tls_cert = optarg;
      break;
    case OPT_TLS_KEY:
      cfg->tls_key = optarg;
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
