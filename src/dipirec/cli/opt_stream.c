/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"
#include "lib/mux/fec2022.h"

#include "priv.h"

args_status_t rec_opt_stream(rec_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case OPT_RET:
      if (argutil_addrport_parse(optarg, &cfg->ret.family, cfg->ret.addr, sizeof cfg->ret.addr, &cfg->ret.port)) {
        argerr("invalid --ret addr:port: %s", optarg);
        return ARGS_ERR;
      }
      cfg->ret.enabled = 1;
      break;
    case OPT_NO_RET_MC:
      cfg->ret.mc_enabled = 0;
      break;
    case OPT_RET_MC_PORT:
      if (argutil_uint_range(optarg, 1, 65535, &cfg->ret.mc_port)) {
        argerr("invalid --ret-mc-port: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_RET_PT: {
      unsigned v;
      if (argutil_uint_range(optarg, 0, 127, &v)) {
        argerr("invalid --ret-pt: %s (0..127)", optarg);
        return ARGS_ERR;
      }
      cfg->ret.rtx_pt = (unsigned char)v;
      break;
    }
    case OPT_RET_WAIT:
      if (argutil_uint_range(optarg, 1, UINT_MAX, &cfg->ret.wait_ms)) {
        argerr("invalid --ret-wait: %s (ms)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_STRIP:
      if (rec_cfg_strip(cfg, optarg)) {
        argerr("invalid --strip: %s (comma list of NUL,NIT,AIT,EIT,CAT,ECM,EMM,RST,TDT,TOT,INT,LCEVC, or \"none\")", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_PACE:
      cfg->pace = 1;
      break;
    case 'O':
      cfg->iface_out = optarg;
      break;
    case OPT_TTL: {
      unsigned v;
      if (argutil_uint_range(optarg, 0, 255, &v)) {
        argerr("invalid --ttl: %s (0..255)", optarg);
        return ARGS_ERR;
      }
      cfg->out_ttl = (int)v;
      break;
    }
    case OPT_AL_FEC:
      if (fec2022_parse_ld(optarg, &cfg->al_fec_l, &cfg->al_fec_d)) {
        argerr("invalid --al-fec: %s (want L:D, L*D<=400, L<=40)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_AL_FEC_PORT:
      if (argutil_port_parse(optarg, &cfg->al_fec_port)) {
        argerr("invalid --al-fec-port: %s", optarg);
        return ARGS_ERR;
      }
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
