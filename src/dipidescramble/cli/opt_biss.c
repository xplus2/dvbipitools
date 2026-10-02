/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/biss/biss.h"
#include "lib/cas/device_state_core.h"
#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/uriparse.h"
#include "lib/net/netconnect.h"

#include "priv.h"

args_status_t dscr_opt_biss(dscr_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case OPT_BISS2_SW:
      if (biss_parse_hex16(optarg, cfg->biss2_sw)) {
        argerr("invalid --biss2-sw: %s (32 hex chars)", optarg);
        return ARGS_ERR;
      }
      cfg->biss2_sw_given = 1;
      break;
    case OPT_BISS2_ESW:
      if (biss_parse_hex16(optarg, cfg->biss2_esw)) {
        argerr("invalid --biss2-esw: %s (32 hex chars)", optarg);
        return ARGS_ERR;
      }
      cfg->biss2_esw_given = 1;
      break;
    case OPT_BISS2_ID:
      if (biss_parse_hex16(optarg, cfg->biss2_id)) {
        argerr("invalid --biss2-id: %s (32 hex chars)", optarg);
        return ARGS_ERR;
      }
      cfg->biss2_id_given = 1;
      break;
    case OPT_BISS1_SW:
      if (biss1_parse_sw(optarg, cfg->biss1_sw)) {
        argerr("invalid --biss1-sw: %s (12 hex chars)", optarg);
        return ARGS_ERR;
      }
      cfg->biss1_sw_given = 1;
      break;
    case OPT_BISS2_CA_KEY:
      cfg->biss2_ca_key_path = optarg;
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
