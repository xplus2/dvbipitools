/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"
#include "priv.h"

args_status_t sds_opt_rms(sds_opt_t *p, int c) {
  config_t *cfg = p->cfg;
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
    case OPT_RMS_LANG:
      if (strlen(optarg) != 3) {
        argerr("AS000207", "invalid --rms-lang: %s (3-letter ISO 639-2 code)", optarg);
        return ARGS_ERR;
      }
      memcpy(cfg->rms_lang, optarg, 3);
      cfg->fl.have_rms_lang = 1;
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
    case OPT_FUS_LANG:
      if (strlen(optarg) != 3) {
        argerr("AS000208", "invalid --fus-lang: %s (3-letter ISO 639-2 code)", optarg);
        return ARGS_ERR;
      }
      memcpy(cfg->fus_lang, optarg, 3);
      cfg->fl.have_fus_lang = 1;
      break;
    case OPT_FUS_ID: {
      unsigned long v;
      if (parse_ulong(optarg, ULONG_MAX, &v)) {
        argerr("AS000209", "invalid --fus-id: %s", optarg);
        return ARGS_ERR;
      }
      cfg->fus_id = v;
      cfg->fl.have_fus_id = 1;
      break;
    }
    case OPT_FUS_ANNOUNCE:
      if (sds_mcast_addr_parse(optarg, cfg->fus_announce_addr, sizeof cfg->fus_announce_addr, &cfg->fus_announce_port)) {
        argerr("AS000210", "invalid --fus-announce: %s", optarg);
        return ARGS_ERR;
      }
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
    case OPT_BCG_MCAST:
      if (sds_mcast_addr_parse(optarg, cfg->bcg_mcast_addr, sizeof cfg->bcg_mcast_addr, &cfg->bcg_mcast_port)) {
        argerr("AS000734", "invalid --bcg-mcast: %s", optarg);
        return ARGS_ERR;
      }
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
