/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>

#include "lib/helper/argutil.h"
#include "lib/mux/fec2022.h"
#include "priv.h"

args_status_t dipirist_opt_rist(dipirist_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case OPT_PROFILE: {
      static const enum_map_t map[] = {{"simple", RIST_PROF_SIMPLE}, {"main", RIST_PROF_MAIN}};
      int v;
      if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
        argerr("VS000151", "invalid --profile: %s (simple|main)", optarg);
        return ARGS_ERR;
      }
      cfg->profile = (rist_profile_sel_t)v;
      break;
    }
    case OPT_SECRET:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->secret, sizeof cfg->secret, optarg, "--secret"))
        return ARGS_ERR;
      break;
    case OPT_ENCRYPTION_TYPE:
      if (argutil_rist_key_size(optarg, &cfg->key_size)) {
        argerr("VS000152", "invalid --encryption-type: %s (128|256)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_CNAME:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->cname, sizeof cfg->cname, optarg, "--cname"))
        return ARGS_ERR;
      break;
    case OPT_BUFFER:
      if (argutil_uint_range(optarg, 1, 60000, &cfg->buffer_ms)) {
        argerr("VS000153", "invalid --buffer: %s (1..60000 ms)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_AL_FEC:
      if (fec2022_parse_ld(optarg, &cfg->al_fec_l, &cfg->al_fec_d)) {
        argerr("VS000154", "invalid --al-fec: %s (want L:D, L*D<=400, L<=40)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_AL_FEC_PORT:
      if (argutil_port_parse(optarg, &cfg->al_fec_port)) {
        argerr("VS000155", "invalid --al-fec-port: %s", optarg);
        return ARGS_ERR;
      }
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
