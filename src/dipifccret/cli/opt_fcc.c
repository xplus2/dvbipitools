/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "lib/helper/argutil.h"
#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const yamlcfg_opt_t opts[] = {
    {'B', "buffer", "FS000433", "invalid -B buffer: %s (ms)", 0},
    {'F', "ff-port", "FS000434", "invalid -F ff-port: %s", 0},
    {OPT_MAX_RET_CLIENTS, "max-ret-clients", "FS000435", "invalid --max-ret-clients: %s", 0},
    {OPT_RET_CLIENT_IDLE_TIMEOUT, "ret.client-idle-timeout", "FS000436", "invalid --ret-client-idle-timeout: %s (s)", 0},
    {OPT_RET_CLIENT_RATE, "ret.client-rate", "FS000437", "invalid --ret-client-rate: %s (packets/s, 0 = unlimited)", 0},
    {OPT_RET_MC_DEDUP, "ret.mc-dedup", "FS000438", "invalid --ret-mc-dedup: %s (ms, 0 = off)", 0},
    {OPT_RSI_INTERVAL, "rsi.interval", "FS000439", "invalid --rsi-interval: %s (s)", 0},
    {OPT_RSI_HOSTNAME, "rsi.hostname", "FS000440", "--rsi-hostname too long: %s", 0},
    {'G', "gop-cap", "FS000441", "invalid -G gop-cap: %s (ms)", 0},
    {'C', "max-bursts", "FS000442", "invalid -C max-bursts: %s", 0},
    {'X', "burst-multiplier", "FS000443", "invalid -X burst-multiplier: %s (1.0 < x <= 1e6)", 0},
    {'D', "burst-duration-cap", "FS000444", "invalid -D burst-duration-cap: %s (ms)", 0},
    {OPT_MAX_BUFFER_FILL_BOUND, "max-buffer-fill-bound", "FS000445", "invalid --max-buffer-fill-bound: %s (ms, 0 = no bound)", 0},
    {OPT_FCC_RESOLVE_BASE_PORT, "fcc.resolve-base-port", "FS000205", "invalid --fcc-resolve-base-port: %s", 0},
    {OPT_CONGESTION_NACK_THRESHOLD, "congestion-nack-threshold", "FS000446", "invalid --congestion-nack-threshold: %s (0 = disabled)", 0},
    {OPT_FCC_RANGE, "fcc.range", "FS000447", "invalid --fcc-range: %s", 0},
    {OPT_FCC_CLIENT_RANGE, "fcc.client-range", "FS000448", "invalid --fcc-client-range: %s", 0},
    {OPT_FCC_CLIENT_RATE, "fcc.client-rate", "FS000449", "invalid --fcc-client-rate: %s (requests/s, 0 = unlimited)", 0},
};

args_status_t fccret_opt_fcc(fccret_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), fccret_cfg_keys, fccret_cfg_nkeys, cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1: return ARGS_ERR;
    default: break;
  }
  switch (c) {
    case OPT_NO_RET:
      cfg->no_ret = 1;
      break;
    case OPT_NO_MC_RET:
      cfg->no_mc_ret = 1;
      break;
    case OPT_NO_RSI:
      cfg->no_rsi = 1;
      break;
    case OPT_RSI_MC_RET:
      cfg->rsi_mc_ret = 1;
      break;
    case OPT_NO_FCC:
      cfg->no_fcc = 1;
      break;
    case OPT_FCC_RESOLVE_BY_PORT:
      cfg->fcc_resolve_by_port = 1;
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
