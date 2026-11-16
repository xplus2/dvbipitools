/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const yamlcfg_opt_t global_opts[] = {
    {OPT_CAS_ALGO, "cas.algo", "TS000610", "--cas-algo: %s", 1},
    {OPT_CAS_CP_DURATION, "cas.cp-duration", "TS000610", "--cas-cp-duration: %s", 1},
    {OPT_BISS2_SW, "biss2.sw", "TS000610", "--biss2-sw: %s", 1},
    {OPT_BISS2_EMIT_ESW, "biss2.emit-esw", "TS000610", "--biss2-emit-esw: %s", 1},
    {OPT_BISS1_SW, "biss1.sw", "TS000610", "--biss1-sw: %s", 1},
    {OPT_BISS1_EMIT_ESW, "biss1.emit-esw", "TS000610", "--biss1-emit-esw: %s", 1},
    {OPT_BISS2_CA_SESSION_ID, "biss2.ca-session-id", "TS000610", "--biss2-ca-session-id: %s", 1},
    {OPT_CAS_PIDS, "cas.pids", "TS000602", "invalid --cas-pids: %s", 0},
};

static const yamlcfg_opt_t vendor_opts[] = {
    {OPT_CAS_ECMG_VERSION, "cas.ecmg.ecmg-version", "TS000610", "--cas-ecmg-version: %s", 1},
    {OPT_CAS_SUPER_ID, "cas.ecmg.super-id", "TS000610", "--cas-super-id: %s", 1},
    {OPT_CAS_ECM_ID, "cas.ecmg.ecm-id", "TS000610", "--cas-ecm-id: %s", 1},
    {OPT_CAS_ECM_PID, "cas.ecmg.ecm-pid", "TS000610", "--cas-ecm-pid: %s", 1},
    {OPT_CAS_EMMG_LISTEN, "cas.ecmg.emmg-listen", "TS000610", "--cas-emmg-listen: %s", 1},
    {OPT_CAS_EMMG_REVERSE, "cas.ecmg.emmg-reverse", "TS000610", "--cas-emmg-reverse: %s", 1},
    {OPT_CAS_EMMG_VERSION, "cas.ecmg.emmg-version", "TS000610", "--cas-emmg-version: %s", 1},
    {OPT_CAS_EMMG_MAX_CONNS, "cas.ecmg.emmg-max-conns", "TS000610", "--cas-emmg-max-conns: %s", 1},
    {OPT_CAS_EMM_PID, "cas.ecmg.emm-pid", "TS000610", "--cas-emm-pid: %s", 1},
    {OPT_CAS_RESILIENCE, "cas.ecmg.resilience", "TS000610", "--cas-resilience: %s", 1},
    {OPT_CAS_CWENC_ALGO, "cas.ecmg.cwenc-algo", "TS000610", "--cas-cwenc-algo: %s", 1},
    {OPT_CAS_CWENC_AES_MODE, "cas.ecmg.cwenc-aes-mode", "TS000610", "--cas-cwenc-aes-mode: %s", 1},
    {OPT_CAS_CWENC_FIXED_KEY, "cas.ecmg.cwenc-fixed-key", "TS000610", "--cas-cwenc-fixed-key: %s", 1},
    {OPT_CAS_CWENC_KEY_LIST_A, "cas.ecmg.cwenc-key-list-a", "TS000610", "--cas-cwenc-key-list-a: %s", 1},
    {OPT_CAS_CWENC_KEY_LIST_B, "cas.ecmg.cwenc-key-list-b", "TS000610", "--cas-cwenc-key-list-b: %s", 1},
};

args_status_t tvh_opt_cas(tvh_opt_t *p, int c) {
  config_t *cfg = p->cfg;
  const cas_vendor_t *vd = p->vd;
  item_state_t saved = cfg->parse_item;
  char err[192];
  int r;

  switch (c) {
    case OPT_CAS_ECMG_VERSION:
      CAS_VENDOR("--cas-ecmg-version");
      break;
    case OPT_CAS_SUPER_ID:
      CAS_VENDOR("--cas-super-id");
      break;
    case OPT_CAS_ECM_ID:
      CAS_VENDOR("--cas-ecm-id");
      break;
    case OPT_CAS_ECM_PID:
      CAS_VENDOR("--cas-ecm-pid");
      break;
    case OPT_CAS_EMMG_LISTEN:
      CAS_VENDOR("--cas-emmg-listen");
      break;
    case OPT_CAS_EMMG_REVERSE:
      CAS_VENDOR("--cas-emmg-reverse");
      break;
    case OPT_CAS_EMMG_VERSION:
      CAS_VENDOR("--cas-emmg-version");
      break;
    case OPT_CAS_EMMG_MAX_CONNS:
      CAS_VENDOR("--cas-emmg-max-conns");
      break;
    case OPT_CAS_EMM_PID:
      CAS_VENDOR("--cas-emm-pid");
      break;
    case OPT_CAS_RESILIENCE:
      CAS_VENDOR("--cas-resilience");
      break;
    case OPT_CAS_CWENC_ALGO:
      CAS_VENDOR("--cas-cwenc-algo");
      break;
    case OPT_CAS_CWENC_AES_MODE:
      CAS_VENDOR("--cas-cwenc-aes-mode");
      break;
    case OPT_CAS_CWENC_FIXED_KEY:
      CAS_VENDOR("--cas-cwenc-fixed-key");
      break;
    case OPT_CAS_CWENC_KEY_LIST_A:
      CAS_VENDOR("--cas-cwenc-key-list-a");
      break;
    case OPT_CAS_CWENC_KEY_LIST_B:
      CAS_VENDOR("--cas-cwenc-key-list-b");
      break;
    case OPT_CAS_ECMG:
      if (!p->cli_vendors) {
        cfg->n_cas_vendors = 0;
        p->cli_vendors = 1;
      }
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_add(cfg->cas_vendors, &cfg->n_cas_vendors, optarg, err, sizeof err), "--cas-ecmg");
      return ARGS_OK;
    case OPT_CAS_REQUIRED:
      CAS_VENDOR("--cas-required");
      cfg->any_cas_flag = 1;
      p->vd->required = 1;
      return ARGS_OK;
    case OPT_CAS_FALLBACK_CLEAR:
      cfg->any_cas_flag = 1;
      cfg->cas_fallback_clear = 1;
      return ARGS_OK;
    case OPT_BISS1_EMIT_ESW_BURIED:
      cfg->biss1_emit_esw_buried = 1;
      return ARGS_OK;
    case OPT_BISS2_CA_RECEIVERS:
      cfg->biss2_ca_receivers_dir = optarg;
      cfg->biss2_ca_enabled = 1;
      return ARGS_OK;
    default:
      break;
  }
  cfg->parse_item.vendor = 1;
  r = yamlcfg_opt_apply(TOOL_NAME, vendor_opts, ARRAY_SIZE(vendor_opts), tvh_cfg_keys, tvh_cfg_nkeys, cfg, c, optarg);
  cfg->parse_item = saved;
  if (!r) r = yamlcfg_opt_apply(TOOL_NAME, global_opts, ARRAY_SIZE(global_opts), tvh_cfg_keys, tvh_cfg_nkeys, cfg, c, optarg);
  if (r > 0) return ARGS_OK;
  return r < 0 ? ARGS_ERR : OPT_UNHANDLED;
}
