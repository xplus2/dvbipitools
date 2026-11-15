/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/helper/argutil.h"
#include "priv.h"

static args_status_t check_announce(config_t *cfg) {
  if (!cfg->input_path) {
    argerr("GS000110", "missing -i input");
    return ARGS_ERR;
  }
  if (!cfg->map_path && cfg->format == FMT_XMLTV) {
    argerr("GS000111", "missing -M map");
    return ARGS_ERR;
  }
  if (cfg->index_mask && (cfg->format != FMT_XMLTV || cfg->encoding != ENC_BIM)) {
    argerr("GS000765", "--index needs -f xmltv and --encoding bim");
    return ARGS_ERR;
  }
  if (cfg->lang && !cfg->publisher) {
    argerr("GS000739", "--lang requires --publisher");
    return ARGS_ERR;
  }
  if (cfg->fl.have_t) {
    if (cfg->fl.t_opt == OPT_TIMEOUT) {
      argerr("GS000690", "--timeout is listen-only (use -t/--interval)");
      return ARGS_ERR;
    }
    if (cfg->fl.t_value < 1) {
      argerr("GS000124", "invalid -t interval: must be >= 1");
      return ARGS_ERR;
    }
    cfg->interval_s = cfg->fl.t_value;
  }
  return ARGS_OK;
}

static args_status_t check_listen(config_t *cfg) {
  if (cfg->metrics_id) {
    argerr("GS000112", "--metrics-id is announce-only");
    return ARGS_ERR;
  }
  if (cfg->compress) {
    argerr("GS000113", "-Z/--compress is announce-only");
    return ARGS_ERR;
  }
  if (cfg->refresh_cycles != 1) {
    argerr("GS000692", "--refresh is announce-only");
    return ARGS_ERR;
  }
  if (cfg->fl.have_encoding) {
    argerr("GS000766", "--encoding is announce-only");
    return ARGS_ERR;
  }
  if (cfg->publisher || cfg->lang || cfg->index_mask) {
    argerr("GS000741", "--publisher, --lang and --index are announce-only");
    return ARGS_ERR;
  }
  if (cfg->fl.have_ttl) {
    argerr("GS000776", "-T/--ttl is announce-only");
    return ARGS_ERR;
  }
  if (cfg->dvb_locator || cfg->dvb_duration || cfg->dvb_pubtime) {
    argerr("GS000760", "--dvb-locator, --dvb-duration and --dvb-pubtime are announce-only");
    return ARGS_ERR;
  }
  if (!cfg->output_path) cfg->output_path = "-";
  if (cfg->fl.have_t) {
    if (cfg->fl.t_opt == OPT_INTERVAL) {
      argerr("GS000691", "--interval is announce-only (use -t/--timeout)");
      return ARGS_ERR;
    }
    cfg->timeout_s = cfg->fl.t_value;
  }
  return ARGS_OK;
}

args_status_t bcg_cli_check(config_t *cfg) {
  cfg->mode = cfg->fl.have_l ? MODE_LISTEN : MODE_ANNOUNCE;
  if (cfg->fl.have_a == cfg->fl.have_l) {
    argerr("GS000108", "exactly one of -a/--announce or -l/--listen is required");
    return ARGS_ERR;
  }
  if (!cfg->fl.have_mcast) {
    argerr("GS000109", "missing -m multicast group:port");
    return ARGS_ERR;
  }
  if (!cfg->nschemas) {
    argerr("GS000759", "missing -s schema");
    return ARGS_ERR;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) return ARGS_ERR;
  return cfg->mode == MODE_ANNOUNCE ? check_announce(cfg) : check_listen(cfg);
}
