/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"
#include "lib/helper/strutil.h"

static const yamlcfg_opt_t global_opts[] = {
    {'T', "ttl", "RS000495", "invalid -T ttl: %s (1..255)", 0},
    {'n', "nit", "LS000685", "-n nit-text too long", 0},
    {OPT_DEFAULT_PROVIDER, "default-provider", "LS000685", "--default-provider text too long", 0},
    {'e', "error", "RS000498", "invalid -e seconds: %s", 0},
    {OPT_TSID, "tsid", "RS000499", "invalid --tsid: %s (1..65535)", 0},
    {OPT_ONID, "onid", "RS000500", "invalid --onid: %s (1..65535)", 0},
};

static const yamlcfg_opt_t input_opts[] = {
    {'s', "input.sdt", "LS000685", "-s sdt-text too long", 0},
    {OPT_PROVIDER, "input.provider", "LS000685", "--provider text too long", 0},
    {OPT_SID, "input.sid", "RS000502", "invalid --sid: %s (1..65535)", 0},
    {OPT_JITTER_MS, "input.jitter-ms", "RS000504", "invalid --jitter-ms: %s (1.." STRINGIFY(RADIOHEAD_MAX_JITTER_MS) " ms)", 0},
};

args_status_t rdh_opt_general(rdh_opt_t *p, int c) {
  config_t *cfg = p->cfg;
  item_state_t saved = cfg->parse_item;
  int r;

  switch (c) {
    case 's':
      if (!p->cli_inputs) {
        argerr("RS000496", "--sdt/-s must follow -i");
        return ARGS_ERR;
      }
      break;
    case OPT_PROVIDER:
      if (!p->cli_inputs) {
        argerr("RS000497", "--provider must follow -i");
        return ARGS_ERR;
      }
      break;
    case OPT_SID:
      if (!p->cli_inputs) {
        argerr("RS000501", "--sid must follow -i");
        return ARGS_ERR;
      }
      break;
    case OPT_JITTER_MS:
      if (!p->cli_inputs) {
        argerr("RS000503", "--jitter-ms must follow -i");
        return ARGS_ERR;
      }
      break;
    default:
      break;
  }
  cfg->parse_item.input = 1;
  cfg->parse_item.have_input = 1;
  r = yamlcfg_opt_apply(TOOL_NAME, input_opts, ARRAY_SIZE(input_opts), rdh_cfg_keys, rdh_cfg_nkeys, cfg, c, optarg);
  cfg->parse_item = saved;
  if (!r) r = yamlcfg_opt_apply(TOOL_NAME, global_opts, ARRAY_SIZE(global_opts), rdh_cfg_keys, rdh_cfg_nkeys, cfg, c, optarg);
  if (r) return r > 0 ? ARGS_OK : ARGS_ERR;
  switch (c) {
    case 'i':
      if (!p->cli_inputs) {
        cfg->n_inputs = 0;
        p->cli_inputs = 1;
      }
      if (cfg->n_inputs >= RADIOHEAD_MAX_INPUTS) {
        argerr("RS000494", "too many -i inputs (max %d)", RADIOHEAD_MAX_INPUTS);
        return ARGS_ERR;
      }
      memset(&cfg->inputs[cfg->n_inputs], 0, sizeof cfg->inputs[0]);
      cfg->inputs[cfg->n_inputs].uri = optarg;
      cfg->n_inputs++;
      break;
    case 'm':
      if (rdh_mcast_parse(optarg, cfg)) {
        argerr("RS000103", "invalid -m group:port: %s", optarg);
        return ARGS_ERR;
      }
      p->have_mcast = 1;
      break;
    case 'O':
      cfg->iface = optarg;
      break;
    case 'r':
      cfg->rtp = 1;
      break;
    case 'k':
      cfg->insecure_tls = 1;
      break;
    case OPT_COLOR: {
      if (argutil_color_opt(TOOL_NAME, "RS000105", optarg, &cfg->color_mode)) return ARGS_ERR;
      break;
    }
    case OPT_METRICS:
      cfg->metrics_sock = optarg;
      break;
    case OPT_METRICS_ID:
      cfg->metrics_id = optarg;
      break;
    case OPT_METRICS_INTERVAL:
      if (argutil_metrics_interval_opt(TOOL_NAME, optarg, &cfg->metrics_interval_s)) return ARGS_ERR;
      break;
    case OPT_METRICS_INSPECT_TS:
      if (argutil_metrics_inspect_ts_opt(TOOL_NAME, optarg, &cfg->metrics_inspect_ts)) return ARGS_ERR;
      break;
    case OPT_METRICS_INSPECT_TS_PIDS:
      if (argutil_metrics_known_pids_opt(TOOL_NAME, optarg, cfg->metrics_known_pids, &cfg->metrics_n_known_pids)) return ARGS_ERR;
      break;
    case 'v':
      cfg->verbose = 1;
      break;
    case 'd':
      cfg->daemonize = 1;
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
