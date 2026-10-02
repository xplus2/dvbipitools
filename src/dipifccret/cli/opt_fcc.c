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

args_status_t fccret_opt_fcc(fccret_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case OPT_NO_RET:
      cfg->no_ret = 1;
      break;
    case 'B':
      if (argutil_uint_range(optarg, 1, UINT_MAX, &cfg->buffer_ms)) {
        argerr("invalid -B buffer: %s (ms)", optarg);
        return ARGS_ERR;
      }
      break;
    case 'F':
      if (argutil_uint_range(optarg, 0, 65535, &cfg->ff_port)) {
        argerr("invalid -F ff-port: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_NO_MC_RET:
      cfg->no_mc_ret = 1;
      break;
    case OPT_MAX_RET_CLIENTS: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
        argerr("invalid --max-ret-clients: %s", optarg);
        return ARGS_ERR;
      }
      cfg->max_ret_clients = (size_t)v;
      break;
    }
    case OPT_RET_CLIENT_IDLE_TIMEOUT:
      if (argutil_uint_range(optarg, 0, UINT_MAX, &cfg->ret_client_idle_timeout_s)) {
        argerr("invalid --ret-client-idle-timeout: %s (s)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_NO_RSI:
      cfg->no_rsi = 1;
      break;
    case OPT_RSI_INTERVAL:
      if (argutil_uint_range(optarg, 1, UINT_MAX, &cfg->rsi_interval_s)) {
        argerr("invalid --rsi-interval: %s (s)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_RSI_MC_RET:
      cfg->rsi_mc_ret = 1;
      break;
    case OPT_RSI_HOSTNAME:
      if (strlen(optarg) >= sizeof cfg->rsi_hostname) {
        argerr("--rsi-hostname too long: %s", optarg);
        return ARGS_ERR;
      }
      bufcpy(cfg->rsi_hostname, sizeof cfg->rsi_hostname, optarg);
      break;
    case OPT_NO_FCC:
      cfg->no_fcc = 1;
      break;
    case 'G':
      if (argutil_uint_range(optarg, 1, UINT_MAX, &cfg->gop_cap_ms)) {
        argerr("invalid -G gop-cap: %s (ms)", optarg);
        return ARGS_ERR;
      }
      break;
    case 'C': {
      unsigned v;
      if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
        argerr("invalid -C max-bursts: %s", optarg);
        return ARGS_ERR;
      }
      cfg->max_bursts = (size_t)v;
      break;
    }
    case 'X': {
      char *end;
      double v = strtod(optarg, &end);
      if (*end != '\0' || v <= 1.0) {
        argerr("invalid -X burst-multiplier: %s (must be > 1.0)", optarg);
        return ARGS_ERR;
      }
      cfg->burst_multiplier = v;
      break;
    }
    case 'D':
      if (argutil_uint_range(optarg, 1, UINT_MAX, &cfg->duration_cap_ms)) {
        argerr("invalid -D burst-duration-cap: %s (ms)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_MAX_BUFFER_FILL_BOUND:
      if (argutil_uint_range(optarg, 0, UINT_MAX, &cfg->max_buffer_fill_bound_ms)) {
        argerr("invalid --max-buffer-fill-bound: %s (ms, 0 = no bound)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_FCC_RESOLVE_BY_PORT:
      cfg->fcc_resolve_by_port = 1;
      break;
    case OPT_FCC_RESOLVE_BASE_PORT:
      if (argutil_uint_range(optarg, 0, 65535, &cfg->fcc_resolve_base_port)) {
        argerr("invalid --fcc-resolve-base-port: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_CONGESTION_NACK_THRESHOLD:
      if (argutil_uint_range(optarg, 0, UINT_MAX, &cfg->congestion_nack_threshold)) {
        argerr("invalid --congestion-nack-threshold: %s (0 = disabled)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_FCC_RANGE:
      if (fccret_cfg_fcc_range(cfg, optarg)) {
        argerr("invalid --fcc-range: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_FCC_CLIENT_RANGE:
      if (fccret_cfg_fcc_client_range(cfg, optarg)) {
        argerr("invalid --fcc-client-range: %s", optarg);
        return ARGS_ERR;
      }
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
