/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/uriparse.h"
#include "lib/net/netconnect.h"

#include "priv.h"

args_status_t sds_opt_ret(sds_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case OPT_RET_ADDR:
      if (sds_ret_addr_parse(optarg, cfg->ret_addr, sizeof cfg->ret_addr, &cfg->ret_port)) {
        argerr("invalid --ret-addr: %s", optarg);
        return ARGS_ERR;
      }
      cfg->ret_enabled = 1;
      break;
    case OPT_RET_RTX_TIME: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
        argerr("invalid --ret-rtx-time: %s", optarg);
        return ARGS_ERR;
      }
      cfg->ret_rtx_time = v;
      cfg->fl.have_ret_rtx_time = 1;
      break;
    }
    case OPT_RET_RTX_PT: {
      unsigned v;
      if (argutil_uint_range(optarg, 0, 127, &v)) {
        argerr("invalid --ret-rtx-pt: %s (0..127)", optarg);
        return ARGS_ERR;
      }
      cfg->ret_rtx_pt = (unsigned char)v;
      cfg->fl.have_ret_rtx_pt = 1;
      break;
    }
    case OPT_RET_MC:
      cfg->ret_mc = 1;
      break;
    case OPT_RET_MC_PORT: {
      unsigned v;
      if (argutil_port_parse(optarg, &v)) {
        argerr("invalid --ret-mc-port: %s", optarg);
        return ARGS_ERR;
      }
      cfg->ret_mc_port = v;
      cfg->fl.have_ret_mc_port = 1;
      break;
    }
    case OPT_RET_RSI_MC_RET:
      cfg->ret_rsi_mc_ret = 1;
      break;
    case OPT_FCC_ADDR:
      if (sds_ret_addr_parse(optarg, cfg->fcc_addr, sizeof cfg->fcc_addr, &cfg->fcc_port)) {
        argerr("invalid --fcc-addr: %s", optarg);
        return ARGS_ERR;
      }
      cfg->fcc_enabled = 1;
      break;
    case OPT_AL_FEC_ADDR:
      if (sds_ret_addr_parse(optarg, cfg->al_fec_addr, sizeof cfg->al_fec_addr, &cfg->al_fec_port)) {
        argerr("invalid --al-fec-addr: %s", optarg);
        return ARGS_ERR;
      }
      cfg->al_fec_enabled = 1;
      break;
    case OPT_AL_FEC_PT: {
      unsigned v;
      if (argutil_uint_range(optarg, 0, 127, &v)) {
        argerr("invalid --al-fec-pt: %s (0..127)", optarg);
        return ARGS_ERR;
      }
      cfg->al_fec_pt = (unsigned char)v;
      cfg->fl.have_al_fec_pt = 1;
      break;
    }
    case OPT_FCC_RTX_TIME: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
        argerr("invalid --fcc-rtx-time: %s", optarg);
        return ARGS_ERR;
      }
      cfg->fcc_rtx_time = v;
      cfg->fl.have_fcc_rtx_time = 1;
      break;
    }
    case OPT_FCC_RTX_PT: {
      unsigned v;
      if (argutil_uint_range(optarg, 0, 127, &v)) {
        argerr("invalid --fcc-rtx-pt: %s (0..127)", optarg);
        return ARGS_ERR;
      }
      cfg->fcc_rtx_pt = (unsigned char)v;
      cfg->fl.have_fcc_rtx_pt = 1;
      break;
    }
    case OPT_FCC_RESOLVE_BY_PORT:
      cfg->fcc_resolve_by_port = 1;
      break;
    case OPT_FCC_RESOLVE_BASE_PORT: {
      unsigned v;
      if (argutil_port_parse(optarg, &v)) {
        argerr("invalid --fcc-resolve-base-port: %s", optarg);
        return ARGS_ERR;
      }
      cfg->fcc_resolve_base_port = v;
      break;
    }
    case OPT_FCC_RESOLVE_MAX_CHANNELS: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
        argerr("invalid --fcc-resolve-max-channels: %s", optarg);
        return ARGS_ERR;
      }
      cfg->fcc_resolve_max_channels = (size_t)v;
      cfg->fl.have_fcc_resolve_max_channels = 1;
      break;
    }
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
