/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <stdlib.h>

#include "lib/helper/argutil.h"
#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const yamlcfg_opt_t opts[] = {
    {OPT_RET_ADDR, "ret.addr", "AS000196", "invalid --ret-addr: %s", 0},
    {OPT_RET_RTX_TIME, "ret.rtx-time", "AS000197", "invalid --ret-rtx-time: %s", 0},
    {OPT_RET_RTX_PT, "ret.rtx-pt", "AS000198", "invalid --ret-rtx-pt: %s (0..127)", 0},
    {OPT_RET_MC_PORT, "ret.mc-port", "AS000199", "invalid --ret-mc-port: %s", 0},
    {OPT_FCC_ADDR, "fcc.addr", "AS000200", "invalid --fcc-addr: %s", 0},
    {OPT_AL_FEC_ADDR, "al-fec.addr", "AS000201", "invalid --al-fec-addr: %s", 0},
    {OPT_AL_FEC_PT, "al-fec.pt", "AS000202", "invalid --al-fec-pt: %s (0..127)", 0},
    {OPT_FCC_RTX_TIME, "fcc.rtx-time", "AS000203", "invalid --fcc-rtx-time: %s", 0},
    {OPT_FCC_RTX_PT, "fcc.rtx-pt", "AS000204", "invalid --fcc-rtx-pt: %s (0..127)", 0},
    {OPT_FCC_RESOLVE_BASE_PORT, "fcc.resolve-base-port", "AS000205", "invalid --fcc-resolve-base-port: %s", 0},
    {OPT_FCC_RESOLVE_MAX_CHANNELS, "fcc.resolve-max-channels", "AS000206", "invalid --fcc-resolve-max-channels: %s", 0},
};

args_status_t sds_opt_ret(sds_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), sds_cfg_keys, sds_cfg_nkeys, cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1: return ARGS_ERR;
    default: break;
  }
  switch (c) {
    case OPT_RET_MC:
      cfg->ret_mc = 1;
      break;
    case OPT_RET_RSI_MC_RET:
      cfg->ret_rsi_mc_ret = 1;
      break;
    case OPT_FCC_RESOLVE_BY_PORT:
      cfg->fcc_resolve_by_port = 1;
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
