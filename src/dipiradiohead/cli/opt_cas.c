/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "lib/cas/cas_args.h"
#include "lib/cas/emmg_server/emmg_server.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"
#include "priv.h"

static const yamlcfg_opt_t global_opts[] = {
    {OPT_CAS_ALGO, "cas.algo", "RS000476", "invalid --cas-algo: %s (cissa|csa2|csa1)", 0},
    {OPT_CAS_CP_DURATION, "cas.cp-duration", "RS000488", "invalid --cas-cp-duration: %s (ms, 1..86400000)", 0},
    {OPT_BISS2_SW, "biss2.sw", "RS000344", "invalid --biss2-sw: %s (32 hex chars)", 0},
    {OPT_BISS2_EMIT_ESW, "biss2.emit-esw", "RS000492", "invalid --biss2-emit-esw: %s (32 hex chars)", 0},
    {OPT_BISS1_SW, "biss1.sw", "RS000347", "invalid --biss1-sw: %s (12 hex chars)", 0},
    {OPT_BISS1_EMIT_ESW, "biss1.emit-esw", "RS000786", "invalid --biss1-emit-esw: %s (14 hex chars)", 0},
    {OPT_BISS2_CA_SESSION_ID, "biss2.ca-session-id", "RS000493", "invalid --biss2-ca-session-id: %s (16 bit, dec or 0x-hex)", 0},
};

static const yamlcfg_opt_t vendor_opts[] = {
    {OPT_CAS_ECMG_VERSION, "cas.ecmg.ecmg-version", "RS000479", "invalid --cas-ecmg-version: %s (2|3)", 0},
    {OPT_CAS_SUPER_ID, "cas.ecmg.super-id", "RS000480", "invalid --cas-super-id: %s", 0},
    {OPT_CAS_ECM_ID, "cas.ecmg.ecm-id", "RS000481", "invalid --cas-ecm-id: %s (1..65535)", 0},
    {OPT_CAS_ECM_PID, "cas.ecmg.ecm-pid", "RS000482", "invalid --cas-ecm-pid: %s (0x0001..0x1FFE)", 0},
    {OPT_CAS_EMMG_REVERSE, "cas.ecmg.emmg-reverse", "RS000484", "invalid --cas-emmg-reverse endpoint: %s", 0},
    {OPT_CAS_EMMG_VERSION, "cas.ecmg.emmg-version", "RS000485", "invalid --cas-emmg-version: %s (2|3)", 0},
    {OPT_CAS_EMM_PID, "cas.ecmg.emm-pid", "RS000487", "invalid --cas-emm-pid: %s (0x0001..0x1FFE)", 0},
    {OPT_CAS_RESILIENCE, "cas.ecmg.resilience", "RS000489", "invalid --cas-resilience: %s (frozen|cycling|silent)", 0},
    {OPT_CAS_CWENC_ALGO, "cas.ecmg.cwenc-algo", "RS000490", "invalid --cas-cwenc-algo: %s (des56|aes128|aes256)", 0},
    {OPT_CAS_CWENC_AES_MODE, "cas.ecmg.cwenc-aes-mode", "RS000491", "invalid --cas-cwenc-aes-mode: %s (stream|ecb)", 0},
    {OPT_CAS_CWENC_FIXED_KEY, "cas.ecmg.cwenc-fixed-key", "LS000685", "--cas-cwenc-fixed-key too long", 0},
    {OPT_CAS_CWENC_KEY_LIST_A, "cas.ecmg.cwenc-key-list-a", "LS000685", "--cas-cwenc-key-list-a too long", 0},
    {OPT_CAS_CWENC_KEY_LIST_B, "cas.ecmg.cwenc-key-list-b", "LS000685", "--cas-cwenc-key-list-b too long", 0},
};

static const char *cas_flag_name(int c) {
  switch (c) {
    case OPT_CAS_ECMG_VERSION: return "cas-ecmg-version";
    case OPT_CAS_SUPER_ID: return "cas-super-id";
    case OPT_CAS_ECM_ID: return "cas-ecm-id";
    case OPT_CAS_ECM_PID: return "cas-ecm-pid";
    case OPT_CAS_EMMG_REVERSE: return "cas-emmg-reverse";
    case OPT_CAS_EMMG_VERSION: return "cas-emmg-version";
    case OPT_CAS_EMM_PID: return "cas-emm-pid";
    case OPT_CAS_RESILIENCE: return "cas-resilience";
    case OPT_CAS_CWENC_ALGO: return "cas-cwenc-algo";
    case OPT_CAS_CWENC_AES_MODE: return "cas-cwenc-aes-mode";
    case OPT_CAS_CWENC_FIXED_KEY: return "cas-cwenc-fixed-key";
    case OPT_CAS_CWENC_KEY_LIST_A: return "cas-cwenc-key-list-a";
    case OPT_CAS_CWENC_KEY_LIST_B: return "cas-cwenc-key-list-b";
    default: return "";
  }
}

static args_status_t opt_status(int r) { return r > 0 ? ARGS_OK : r < 0 ? ARGS_ERR : OPT_UNHANDLED; }

static args_status_t vendor_apply(rdh_opt_t *p, int c) {
  config_t *cfg = p->cfg;
  item_state_t saved = cfg->parse_item;
  int r;
  cfg->parse_item.vendor = 1;
  cfg->parse_item.have_vendor = 1;
  r = yamlcfg_opt_apply(TOOL_NAME, vendor_opts, ARRAY_SIZE(vendor_opts), rdh_cfg_keys, rdh_cfg_nkeys, cfg, c, optarg);
  cfg->parse_item = saved;
  return opt_status(r);
}

args_status_t rdh_opt_cas(rdh_opt_t *p, int c) {
  config_t *cfg = p->cfg;
  cas_vendor_t *vend;
  char err[128];
  int r;

  switch (c) {
    case OPT_CAS_ECMG_VERSION:
    case OPT_CAS_SUPER_ID:
    case OPT_CAS_ECM_ID:
    case OPT_CAS_ECM_PID:
    case OPT_CAS_EMMG_REVERSE:
    case OPT_CAS_EMMG_VERSION:
    case OPT_CAS_EMM_PID:
    case OPT_CAS_RESILIENCE:
    case OPT_CAS_CWENC_ALGO:
    case OPT_CAS_CWENC_AES_MODE:
    case OPT_CAS_CWENC_FIXED_KEY:
    case OPT_CAS_CWENC_KEY_LIST_A:
    case OPT_CAS_CWENC_KEY_LIST_B:
      p->any_cas_flag = 1;
      if (!rdh_current_cas_vendor(cfg, p->cli_vendors, cas_flag_name(c))) return ARGS_ERR;
      return vendor_apply(p, c);
    case OPT_CAS_ALGO:
    case OPT_CAS_CP_DURATION:
      p->any_cas_flag = 1;
      break;
    case OPT_CAS_ECMG:
      p->any_cas_flag = 1;
      if (!p->cli_vendors) {
        cfg->n_cas_vendors = 0;
        p->cli_vendors = 1;
      }
      if (cfg->n_cas_vendors >= ARGS_MAX_CAS_VENDORS) {
        argerr("RS000477", "too many --cas-ecmg vendors (max %d)", ARGS_MAX_CAS_VENDORS);
        return ARGS_ERR;
      }
      if (cas_vendor_add(cfg->cas_vendors, &cfg->n_cas_vendors, optarg, err, sizeof err)) {
        argerr("RS000478", "invalid --cas-ecmg endpoint: %s", optarg);
        return ARGS_ERR;
      }
      return ARGS_OK;
    case OPT_CAS_EMMG_LISTEN:
      p->any_cas_flag = 1;
      vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-emmg-listen");
      if (!vend) return ARGS_ERR;
      if (cas_vendor_set_emmg_listen(vend, optarg, err, sizeof err)) {
        argerr("RS000483", "--cas-emmg-listen: %s", err);
        return ARGS_ERR;
      }
      return ARGS_OK;
    case OPT_CAS_EMMG_MAX_CONNS:
      p->any_cas_flag = 1;
      vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-emmg-max-conns");
      if (!vend) return ARGS_ERR;
      if (cas_vendor_set_emmg_max_conns(vend, optarg, err, sizeof err)) {
        argerr("RS000486", "invalid --cas-emmg-max-conns: %s (1..%u)", optarg, EMMG_MAX_CONNS_CEILING);
        return ARGS_ERR;
      }
      return ARGS_OK;
    case OPT_CAS_REQUIRED:
      p->any_cas_flag = 1;
      vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-required");
      if (!vend) return ARGS_ERR;
      vend->required = 1;
      return ARGS_OK;
    case OPT_CAS_FALLBACK_CLEAR:
      p->any_cas_flag = 1;
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
  r = yamlcfg_opt_apply(TOOL_NAME, global_opts, ARRAY_SIZE(global_opts), rdh_cfg_keys, rdh_cfg_nkeys, cfg, c, optarg);
  return opt_status(r);
}
