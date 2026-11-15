/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "lib/helper/argutil.h"
#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const yamlcfg_opt_t opts[] = {
    {OPT_RMS_LANG, "rms.lang", "AS000207", "invalid --rms-lang: %s (3-letter ISO 639-2 code)", 0},
    {OPT_FUS_LANG, "fus.lang", "AS000208", "invalid --fus-lang: %s (3-letter ISO 639-2 code)", 0},
    {OPT_FUS_ID, "fus.id", "AS000209", "invalid --fus-id: %s", 0},
    {OPT_FUS_ANNOUNCE, "fus.announce", "AS000210", "invalid --fus-announce: %s", 0},
    {OPT_BCG_MCAST, "bcg.mcast", "AS000734", "invalid --bcg-mcast: %s", 0},
};

args_status_t sds_opt_rms(sds_opt_t *p, int c) {
  config_t *cfg = p->cfg;
  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), sds_cfg_keys, sds_cfg_nkeys, cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1: return ARGS_ERR;
    default: break;
  }
  switch (c) {
    case OPT_METRICS:
      cfg->metrics_sock = optarg;
      break;
    case OPT_METRICS_ID:
      cfg->metrics_id = optarg;
      break;
    case OPT_METRICS_INTERVAL:
      if (argutil_metrics_interval_opt(TOOL_NAME, optarg, &cfg->metrics_interval_s)) return ARGS_ERR;
      break;
    case OPT_PACKAGES:
      cfg->packages_path = optarg;
      break;
    case OPT_CELLS:
      cfg->cells_path = optarg;
      break;
    case OPT_RMS_NAME:
      cfg->rms_name = optarg;
      cfg->rms_enabled = 1;
      break;
    case OPT_RMS_LOCATION:
      cfg->rms_location = optarg;
      break;
    case OPT_RMS_LOGO:
      cfg->rms_logo = optarg;
      break;
    case OPT_FUS_NAME:
      cfg->fus_name = optarg;
      cfg->fus_enabled = 1;
      break;
    case OPT_FUS_LOGO:
      cfg->fus_logo = optarg;
      break;
    case OPT_BCG_NAME:
      cfg->bcg_name = optarg;
      cfg->bcg_enabled = 1;
      break;
    case OPT_BCG_ID:
      cfg->bcg_id = optarg;
      break;
    case OPT_BCG_DESCRIPTION:
      cfg->bcg_description = optarg;
      break;
    case OPT_BCG_PROVIDER_NAME:
      cfg->bcg_provider_name = optarg;
      break;
    case OPT_BIM_NAMESPACE:
      cfg->sdns_ns = optarg;
      break;
    case 's':
      if (!cfg->fl.schema_cli) cfg->nschemas = 0;
      cfg->fl.schema_cli = 1;
      if (cfg->nschemas >= SCHEMA_MAX) {
        argerr("AS000753", "too many --schema entries (max %d)", SCHEMA_MAX);
        return ARGS_ERR;
      }
      cfg->schemas[cfg->nschemas++] = optarg;
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
