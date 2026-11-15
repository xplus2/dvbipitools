/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "lib/cas/biss/biss.h"
#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const yamlcfg_opt_t opts[] = {
    {OPT_BISS2_SW, "biss2.sw", "DS000344", "invalid --biss2-sw: %s (32 hex chars)", 0},
    {OPT_BISS2_ESW, "biss2.esw", "DS000345", "invalid --biss2-esw: %s (32 hex chars)", 0},
    {OPT_BISS2_ID, "biss2.id", "DS000346", "invalid --biss2-id: %s (32 hex chars)", 0},
    {OPT_BISS1_SW, "biss1.sw", "DS000347", "invalid --biss1-sw: %s (12 hex chars)", 0},
    {OPT_BISS1_ESW, "biss1.esw", "DS000784", "invalid --biss1-esw: %s (16 hex chars)", 0},
    {OPT_BISS1_ID, "biss1.id", "DS000785", "invalid --biss1-id: %s (14 hex chars)", 0},
};

args_status_t dscr_opt_biss(dscr_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), dscr_cfg_keys, dscr_cfg_nkeys, cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1: return ARGS_ERR;
    default: break;
  }
  switch (c) {
    case OPT_BISS1_ID_BURIED:
      cfg->biss1_id_buried = 1;
      break;
    case OPT_BISS2_CA_KEY:
      if (!p->cli_ca_key) {
        cfg->n_biss2_ca_key = 0;
        p->cli_ca_key = 1;
      }
      if (cfg->n_biss2_ca_key >= DIPIDESCRAMBLE_MAX_CA_KEYS) {
        argerr("DS000348", "too many --biss2-ca-key (max %d)", DIPIDESCRAMBLE_MAX_CA_KEYS);
        return ARGS_ERR;
      }
      cfg->biss2_ca_key[cfg->n_biss2_ca_key++] = optarg;
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
