/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */


#include <getopt.h>

#include "lib/helper/argutil.h"

#include "priv.h"

args_status_t tvh_opt_cas(tvh_opt_t *p, int c) {
  config_t *cfg = p->cfg;
  cas_vendor_t *vd = p->vd;
  char err[192];

  switch (c) {
    case OPT_CAS_ALGO:
      cfg->any_cas_flag = 1;
      CHECK(cas_set_algo(&cfg->cas_algo, optarg, err, sizeof err), "--cas-algo");
      break;
    case OPT_CAS_ECMG:
      if (!p->cli_vendors) {
        cfg->n_cas_vendors = 0;
        p->cli_vendors = 1;
      }
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_add(cfg->cas_vendors, &cfg->n_cas_vendors, optarg, err, sizeof err), "--cas-ecmg");
      break;
    case OPT_CAS_ECMG_VERSION:
      CAS_VENDOR("--cas-ecmg-version");
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_set_ecmg_version(vd, optarg, err, sizeof err), "--cas-ecmg-version");
      break;
    case OPT_CAS_SUPER_ID:
      CAS_VENDOR("--cas-super-id");
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_set_super_id(vd, optarg, err, sizeof err), "--cas-super-id");
      break;
    case OPT_CAS_ECM_ID:
      CAS_VENDOR("--cas-ecm-id");
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_set_ecm_id(vd, optarg, err, sizeof err), "--cas-ecm-id");
      break;
    case OPT_CAS_ECM_PID:
      CAS_VENDOR("--cas-ecm-pid");
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_set_ecm_pid(vd, optarg, err, sizeof err), "--cas-ecm-pid");
      break;
    case OPT_CAS_EMMG_PORT:
      CAS_VENDOR("--cas-emmg-port");
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_set_emmg_port(vd, optarg, err, sizeof err), "--cas-emmg-port");
      break;
    case OPT_CAS_EMMG_REVERSE:
      CAS_VENDOR("--cas-emmg-reverse");
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_set_emmg_reverse(vd, optarg, err, sizeof err), "--cas-emmg-reverse");
      break;
    case OPT_CAS_EMMG_VERSION:
      CAS_VENDOR("--cas-emmg-version");
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_set_emmg_version(vd, optarg, err, sizeof err), "--cas-emmg-version");
      break;
    case OPT_CAS_EMMG_MAX_CONNS:
      CAS_VENDOR("--cas-emmg-max-conns");
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_set_emmg_max_conns(vd, optarg, err, sizeof err), "--cas-emmg-max-conns");
      break;
    case OPT_CAS_EMM_PID:
      CAS_VENDOR("--cas-emm-pid");
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_set_emm_pid(vd, optarg, err, sizeof err), "--cas-emm-pid");
      break;
    case OPT_CAS_PIDS:
      cfg->any_cas_flag = 1;
      if (tvh_cfg_cas_pids(cfg, optarg)) {
        argerr("invalid --cas-pids: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_CAS_CP_DURATION:
      cfg->any_cas_flag = 1;
      CHECK(cas_set_cp_duration(&cfg->cas_cp_duration_ms, optarg, err, sizeof err), "--cas-cp-duration");
      break;
    case OPT_CAS_RESILIENCE:
      CAS_VENDOR("--cas-resilience");
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_set_resilience(vd, optarg, err, sizeof err), "--cas-resilience");
      break;
    case OPT_CAS_REQUIRED:
      CAS_VENDOR("--cas-required");
      cfg->any_cas_flag = 1;
      vd->required = 1;
      break;
    case OPT_CAS_CWENC_ALGO:
      CAS_VENDOR("--cas-cwenc-algo");
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_set_cwenc_algo(vd, optarg, err, sizeof err), "--cas-cwenc-algo");
      break;
    case OPT_CAS_CWENC_AES_MODE:
      CAS_VENDOR("--cas-cwenc-aes-mode");
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_set_cwenc_aes_mode(vd, optarg, err, sizeof err), "--cas-cwenc-aes-mode");
      break;
    case OPT_CAS_CWENC_FIXED_KEY:
      CAS_VENDOR("--cas-cwenc-fixed-key");
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_set_cwenc_fixed_key(vd, optarg, err, sizeof err), "--cas-cwenc-fixed-key");
      break;
    case OPT_CAS_CWENC_KEY_LIST_A:
      CAS_VENDOR("--cas-cwenc-key-list-a");
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_set_cwenc_key_list_a(vd, optarg, err, sizeof err), "--cas-cwenc-key-list-a");
      break;
    case OPT_CAS_CWENC_KEY_LIST_B:
      CAS_VENDOR("--cas-cwenc-key-list-b");
      cfg->any_cas_flag = 1;
      CHECK(cas_vendor_set_cwenc_key_list_b(vd, optarg, err, sizeof err), "--cas-cwenc-key-list-b");
      break;
    case OPT_CAS_FALLBACK_CLEAR:
      cfg->any_cas_flag = 1;
      cfg->cas_fallback_clear = 1;
      break;
    case OPT_BISS2_SW:
      CHECK(cas_set_biss2_sw(&cfg->biss2_enabled, cfg->biss2_sw, optarg, err, sizeof err), "--biss2-sw");
      break;
    case OPT_BISS2_EMIT_ESW:
      CHECK(cas_set_biss2_emit_esw(&cfg->biss2_emit_esw, cfg->biss2_esw_id, optarg, err, sizeof err), "--biss2-emit-esw");
      break;
    case OPT_BISS1_SW:
      CHECK(cas_set_biss1_sw(&cfg->biss1_enabled, cfg->biss1_cw, optarg, err, sizeof err), "--biss1-sw");
      break;
    case OPT_BISS2_CA_RECEIVERS:
      cfg->biss2_ca_receivers_dir = optarg;
      cfg->biss2_ca_enabled = 1;
      break;
    case OPT_BISS2_CA_SESSION_ID:
      CHECK(cas_set_biss2_ca_session_id(&cfg->biss2_ca_session_id, &cfg->biss2_ca_session_id_given, optarg, err, sizeof err), "--biss2-ca-session-id");
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
