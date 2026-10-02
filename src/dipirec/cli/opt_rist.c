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

args_status_t rec_opt_rist(rec_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case OPT_PROFILE:
      if (rec_cfg_profile(cfg, optarg, 0)) {
        argerr("invalid --rist-profile: %s (simple|main)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_SECRET:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->rist_secret, sizeof cfg->rist_secret, optarg, "--rist-secret"))
        return ARGS_ERR;
      cfg->fl.have_secret = 1;
      break;
    case OPT_ENCRYPTION_TYPE:
      if (argutil_rist_key_size(optarg, &cfg->rist_key_size)) {
        argerr("invalid --rist-encryption-type: %s (128|256)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_ENCRYPTION_TYPE_IN:
      if (argutil_rist_key_size(optarg, &cfg->rist_key_size_in)) {
        argerr("invalid --rist-encryption-type-in: %s (128|256)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_CNAME:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->rist_cname, sizeof cfg->rist_cname, optarg, "--rist-cname"))
        return ARGS_ERR;
      cfg->fl.have_cname = 1;
      break;
    case OPT_BUFFER:
      if (argutil_uint_range(optarg, 1, UINT_MAX, &cfg->rist_buffer_ms)) {
        argerr("invalid --rist-buffer: %s (ms)", optarg);
        return ARGS_ERR;
      }
      cfg->fl.have_buffer = 1;
      break;
    case OPT_PROFILE_IN:
      if (rec_cfg_profile(cfg, optarg, 1)) {
        argerr("invalid --rist-profile-in: %s (simple|main)", optarg);
        return ARGS_ERR;
      }
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
