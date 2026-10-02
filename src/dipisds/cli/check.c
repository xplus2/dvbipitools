/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"

#include "priv.h"

static args_status_t validate_mode_mcast(const config_t *cfg, const args_flags_t *fl) {
  if (fl->have_a == fl->have_l) {
    argerr("exactly one of -a/--announce or -l/--listen is required");
    return ARGS_ERR;
  }
  if (!fl->have_mcast) {
    argerr("missing -m multicast group:port");
    return ARGS_ERR;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) return ARGS_ERR;
  return ARGS_OK;
}

static args_status_t validate_announce_input(config_t *cfg, const args_flags_t *fl) {
  if (!cfg->input_path) {
    argerr("missing -i input");
    return ARGS_ERR;
  }
  if (!sds_has_suffix(cfg->input_path, ".xml")) {
    if (!cfg->provider) {
      argerr("missing -p provider (required unless -i is .xml)");
      return ARGS_ERR;
    }
    if (!cfg->offering) {
      argerr("missing -O offering (required unless -i is .xml)");
      return ARGS_ERR;
    }
  }
  if (!cfg->lang[0]) memcpy(cfg->lang, "deu", 3);
  if (fl->have_t) cfg->interval_s = fl->t_value;
  return ARGS_OK;
}

static args_status_t validate_announce_ret(config_t *cfg, const args_flags_t *fl) {
  if (cfg->ret_enabled && sds_has_suffix(cfg->input_path, ".xml")) {
    argerr("--ret-addr has no effect with a raw .xml -i input (that path is sent through unparsed)");
    return ARGS_ERR;
  }
  if (!cfg->ret_enabled && (fl->have_ret_rtx_time || fl->have_ret_rtx_pt || cfg->ret_mc || fl->have_ret_mc_port || cfg->ret_rsi_mc_ret)) {
    argerr("--ret-rtx-time/--ret-rtx-pt/--ret-mc/--ret-mc-port/--ret-rsi-mc-ret require --ret-addr");
    return ARGS_ERR;
  }
  if (cfg->ret_rsi_mc_ret && !cfg->ret_mc) {
    argerr("--ret-rsi-mc-ret requires --ret-mc");
    return ARGS_ERR;
  }
  if (cfg->ret_enabled) {
    if (!fl->have_ret_rtx_time) cfg->ret_rtx_time = 2000;
    if (!fl->have_ret_rtx_pt) cfg->ret_rtx_pt = 99;
  }
  return ARGS_OK;
}

static args_status_t validate_announce_fcc(config_t *cfg, const args_flags_t *fl) {
  if (cfg->fcc_enabled && sds_has_suffix(cfg->input_path, ".xml")) {
    argerr("--fcc-addr has no effect with a raw .xml -i input (that path is sent through unparsed)");
    return ARGS_ERR;
  }
  if (!cfg->fcc_enabled && (fl->have_fcc_rtx_time || fl->have_fcc_rtx_pt || cfg->fcc_resolve_by_port || cfg->fcc_resolve_base_port || fl->have_fcc_resolve_max_channels)) {
    argerr("--fcc-rtx-time/--fcc-rtx-pt/--fcc-resolve-* require --fcc-addr");
    return ARGS_ERR;
  }
  if (cfg->fcc_enabled) {
    if (!fl->have_fcc_rtx_time) cfg->fcc_rtx_time = 2000;
    if (!fl->have_fcc_rtx_pt) cfg->fcc_rtx_pt = 99;
    if (!fl->have_fcc_resolve_max_channels) cfg->fcc_resolve_max_channels = 300;
  }
  return ARGS_OK;
}

static args_status_t validate_announce_al_fec(config_t *cfg, const args_flags_t *fl) {
  if (cfg->al_fec_enabled && sds_has_suffix(cfg->input_path, ".xml")) {
    argerr("--al-fec-addr has no effect with a raw .xml -i input (that path is sent through unparsed)");
    return ARGS_ERR;
  }
  if (!cfg->al_fec_enabled && fl->have_al_fec_pt) {
    argerr("--al-fec-pt requires --al-fec-addr");
    return ARGS_ERR;
  }
  if (cfg->al_fec_enabled && !fl->have_al_fec_pt) cfg->al_fec_pt = 96;
  return ARGS_OK;
}

static args_status_t validate_announce_rms_fus(config_t *cfg, const args_flags_t *fl) {
  if ((cfg->packages_path || cfg->cells_path || cfg->rms_enabled || cfg->fus_enabled) && sds_has_suffix(cfg->input_path, ".xml")) {
    argerr("--packages/--cells/--rms-name/--fus-name have no effect with a raw .xml -i input (that path is sent through unparsed)");
    return ARGS_ERR;
  }
  if (cfg->rms_enabled && cfg->fus_enabled) {
    argerr("--rms-name and --fus-name are mutually exclusive (RMSFUSDiscovery carries one or the other, never both)");
    return ARGS_ERR;
  }
  if (!cfg->rms_enabled && (fl->have_rms_lang || cfg->rms_location || cfg->rms_logo)) {
    argerr("--rms-lang/--rms-location/--rms-logo require --rms-name");
    return ARGS_ERR;
  }
  if (cfg->rms_enabled) {
    if (!cfg->rms_location) {
      argerr("--rms-name requires --rms-location");
      return ARGS_ERR;
    }
    if (!fl->have_rms_lang) memcpy(cfg->rms_lang, "deu", 3);
  }
  if (!cfg->fus_enabled && (fl->have_fus_lang || fl->have_fus_id || cfg->fus_announce_addr[0] || cfg->fus_logo)) {
    argerr("--fus-lang/--fus-id/--fus-announce/--fus-logo require --fus-name");
    return ARGS_ERR;
  }
  if (cfg->fus_enabled) {
    if (!fl->have_fus_id) {
      argerr("--fus-name requires --fus-id");
      return ARGS_ERR;
    }
    if (!fl->have_fus_lang) memcpy(cfg->fus_lang, "deu", 3);
  }
  return ARGS_OK;
}

static args_status_t validate_listen(config_t *cfg, const args_flags_t *fl) {
  if (cfg->ret_enabled || fl->have_ret_rtx_time || fl->have_ret_rtx_pt || cfg->ret_mc || fl->have_ret_mc_port || cfg->ret_rsi_mc_ret) {
    argerr("--ret-* options are announce-only");
    return ARGS_ERR;
  }
  if (cfg->fcc_enabled || fl->have_fcc_rtx_time || fl->have_fcc_rtx_pt || cfg->fcc_resolve_by_port || cfg->fcc_resolve_base_port || fl->have_fcc_resolve_max_channels) {
    argerr("--fcc-* options are announce-only");
    return ARGS_ERR;
  }
  if (cfg->metrics_id) {
    argerr("--metrics-id is announce-only");
    return ARGS_ERR;
  }
  if (cfg->packages_path || cfg->cells_path || cfg->rms_enabled || fl->have_rms_lang || cfg->rms_location || cfg->rms_logo ||
      cfg->fus_enabled || fl->have_fus_lang || fl->have_fus_id || cfg->fus_announce_addr[0] || cfg->fus_logo) {
    argerr("--packages/--cells/--rms-*/--fus-* options are announce-only");
    return ARGS_ERR;
  }
  if (!cfg->output_path) cfg->output_path = "-";
  if (!fl->have_format) {
    if (sds_has_suffix(cfg->output_path, ".csv"))       cfg->format = OUT_CSV;
    else if (sds_has_suffix(cfg->output_path, ".xspf")) cfg->format = OUT_XSPF;
    else if (sds_has_suffix(cfg->output_path, ".xml"))  cfg->format = OUT_XML;
  }
  if (fl->have_t) cfg->timeout_s = fl->t_value;
  return ARGS_OK;
}
args_status_t sds_cli_check(config_t *cfg) {
  args_status_t st;

  cfg->mode = cfg->fl.have_l ? MODE_LISTEN : MODE_ANNOUNCE;
  if ((st = validate_mode_mcast(cfg, &cfg->fl)) != ARGS_OK) return st;
  if (cfg->mode == MODE_ANNOUNCE) {
    if ((st = validate_announce_input(cfg, &cfg->fl)) != ARGS_OK) return st;
    if ((st = validate_announce_ret(cfg, &cfg->fl)) != ARGS_OK) return st;
    if ((st = validate_announce_fcc(cfg, &cfg->fl)) != ARGS_OK) return st;
    if ((st = validate_announce_al_fec(cfg, &cfg->fl)) != ARGS_OK) return st;
    if ((st = validate_announce_rms_fus(cfg, &cfg->fl)) != ARGS_OK) return st;
  } else {
    if ((st = validate_listen(cfg, &cfg->fl)) != ARGS_OK) return st;
  }
  return ARGS_OK;
}
