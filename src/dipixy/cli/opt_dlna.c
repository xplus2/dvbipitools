/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"
#include "lib/mux/fec2022.h"

#include "priv.h"

args_status_t dixy_opt_dlna(dixy_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case OPT_SDS_TIMEOUT: {
      char *end;
      double v = strtod(optarg, &end);
      if (*end != '\0' || !isfinite(v) || v <= 0.0 || v > 1e9) {
        argerr("invalid --sds-timeout: %s (seconds, > 0)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->sds_timeout_s = v;
      break;
    }
    case OPT_SDS_REFRESH_INTERVAL: {
      char *end;
      double v = strtod(optarg, &end);
      if (*end != '\0' || !isfinite(v) || v <= 0.0 || v > 1e9) {
        argerr("invalid --sds-refresh-interval: %s (seconds, > 0)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->sds_refresh_interval_s = v;
      break;
    }
    case OPT_SSDP_TTL: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, 255, &v)) {
        argerr("invalid --ssdp-ttl: %s (1..255)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->ssdp_ttl = (int)v;
      break;
    }
    case OPT_SSDP_IFACE:
      cfg->ssdp_iface = optarg;
      break;
    case OPT_SSDP_INTERVAL: {
      char *end;
      double v = strtod(optarg, &end);
      if (*end != '\0' || !isfinite(v) || v <= 0.0 || v > 1e9) {
        argerr("invalid --ssdp-interval: %s (seconds, > 0)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->ssdp_interval_s = v;
      break;
    }
    case OPT_SSDP_MAX_AGE: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
        argerr("invalid --ssdp-max-age: %s (seconds, > 0)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->ssdp_max_age_s = v;
      break;
    }
    case OPT_ENABLE_DLNA:
      cfg->enable_dlna = 1;
      break;
    case OPT_DLNA_HOST:
      cfg->dlna_host_opt = optarg;
      break;
    case OPT_DLNA_NAME:
      cfg->dlna_name = optarg;
      break;
    case OPT_DLNA_KEEP_MULTICAST:
      cfg->dlna_keep_multicast = 1;
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
