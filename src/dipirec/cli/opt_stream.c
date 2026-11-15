/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const yamlcfg_opt_t opts[] = {
    {OPT_RET, "ret.addr", "CS000554", "invalid --ret addr:port: %s", 0},
    {OPT_RET_MC_PORT, "ret.mc-port", "CS000199", "invalid --ret-mc-port: %s", 0},
    {OPT_RET_PT, "ret.pt", "CS000555", "invalid --ret-pt: %s (0..127)", 0},
    {OPT_RET_WAIT, "ret.wait", "CS000556", "invalid --ret-wait: %s (ms)", 0},
    {OPT_STRIP, "strip", "CS000557", "invalid --strip: %s (comma list of NUL,NIT,AIT,EIT,CAT,ECM,EMM,RST,TDT,TOT,INT,LCEVC, or \"none\")", 0},
    {OPT_TTL, "ttl", "CS000558", "invalid --ttl: %s (0..255)", 0},
    {OPT_AL_FEC, "al-fec", "CS000154", "invalid --al-fec: %s (want L:D, L*D<=400, L<=40)", 0},
    {OPT_AL_FEC_PORT, "al-fec-port", "CS000155", "invalid --al-fec-port: %s", 0},
    {OPT_AL_FEC_OUT, "al-fec-out", "CS000702", "invalid --al-fec-out: %s (want L:D, L*D<=400, L<=40)", 0},
    {OPT_AL_FEC_PORT_OUT, "al-fec-port-out", "CS000703", "invalid --al-fec-port-out: %s", 0},
};

args_status_t rec_opt_stream(rec_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), rec_cfg_keys, rec_cfg_nkeys, cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1: return ARGS_ERR;
    default: break;
  }
  switch (c) {
    case OPT_NO_RET_MC:
      cfg->ret.mc_enabled = 0;
      break;
    case OPT_PACE:
      cfg->pace = 1;
      break;
    case 'O':
      cfg->iface_out = optarg;
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
