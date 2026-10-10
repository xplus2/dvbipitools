/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/biss/biss.h"
#include "lib/cas/cas_args.h"
#include "lib/cas/emmg_server/emmg_server.h"
#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/mux/fec2022.h"
#include "lib/net/netconnect.h"

#include "priv.h"

args_status_t rdh_opt_cas(rdh_opt_t *p, int c) {
  config_t *cfg = p->cfg;
  switch (c) {
    case OPT_CAS_ALGO: {
      static const enum_map_t map[] = {{"cissa", CAS_ALGO_CISSA}, {"csa2", CAS_ALGO_CSA2}, {"csa1", CAS_ALGO_CSA1}};
      int v;
      p->any_cas_flag = 1;
      if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
        argerr("invalid --cas-algo: %s (cissa|csa2|csa1)", optarg);
        return ARGS_ERR;
      }
      cfg->cas_algo = (cas_algo_t)v;
      break;
    }
    case OPT_CAS_ECMG: {
      cas_vendor_t *vend;
      p->any_cas_flag = 1;
      if (!p->cli_vendors) {
        cfg->n_cas_vendors = 0;
        p->cli_vendors = 1;
      }
      if (cfg->n_cas_vendors >= ARGS_MAX_CAS_VENDORS) {
        argerr("too many --cas-ecmg vendors (max %d)", ARGS_MAX_CAS_VENDORS);
        return ARGS_ERR;
      }
      vend = &cfg->cas_vendors[cfg->n_cas_vendors];
      memset(vend, 0, sizeof *vend);
      vend->ecm_pid = 0x0020;
      vend->emmg_port = 8002;
      vend->emm_pid = 0x0021;
      if (cas_endpoint_parse(optarg, vend->ecmg_host, sizeof vend->ecmg_host, &vend->ecmg_port)) {
        argerr("invalid --cas-ecmg endpoint: %s", optarg);
        return ARGS_ERR;
      }
      cfg->n_cas_vendors++;
      break;
    }
    case OPT_CAS_ECMG_VERSION: {
      cas_vendor_t *vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-ecmg-version");
      p->any_cas_flag = 1;
      if (!vend) return ARGS_ERR;
      if (cas_version_parse(optarg, &vend->ecmg_version)) {
        argerr("invalid --cas-ecmg-version: %s (2|3)", optarg);
        return ARGS_ERR;
      }
      break;
    }
    case OPT_CAS_SUPER_ID: {
      cas_vendor_t *vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-super-id");
      p->any_cas_flag = 1;
      if (!vend) return ARGS_ERR;
      if (cas_super_id_parse(optarg, &vend->super_cas_id)) {
        argerr("invalid --cas-super-id: %s", optarg);
        return ARGS_ERR;
      }
      break;
    }
    case OPT_CAS_ECM_ID: {
      cas_vendor_t *vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-ecm-id");
      p->any_cas_flag = 1;
      if (!vend) return ARGS_ERR;
      if (rdh_id_parse(optarg, &vend->ecm_id)) {
        argerr("invalid --cas-ecm-id: %s (1..65535)", optarg);
        return ARGS_ERR;
      }
      break;
    }
    case OPT_CAS_ECM_PID: {
      cas_vendor_t *vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-ecm-pid");
      p->any_cas_flag = 1;
      if (!vend) return ARGS_ERR;
      if (rdh_pid_parse(optarg, &vend->ecm_pid)) {
        argerr("invalid --cas-ecm-pid: %s (0x0001..0x1FFE)", optarg);
        return ARGS_ERR;
      }
      break;
    }
    case OPT_CAS_EMMG_LISTEN: {
      cas_vendor_t *vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-emmg-listen");
      char lerr[128];
      p->any_cas_flag = 1;
      if (!vend) return ARGS_ERR;
      if (cas_vendor_set_emmg_listen(vend, optarg, lerr, sizeof lerr)) {
        argerr("--cas-emmg-listen: %s", lerr);
        return ARGS_ERR;
      }
      break;
    }
    case OPT_CAS_EMMG_REVERSE: {
      cas_vendor_t *vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-emmg-reverse");
      p->any_cas_flag = 1;
      if (!vend) return ARGS_ERR;
      if (cas_endpoint_parse(optarg, vend->emmg_reverse_host, sizeof vend->emmg_reverse_host, &vend->emmg_reverse_port)) {
        argerr("invalid --cas-emmg-reverse endpoint: %s", optarg);
        return ARGS_ERR;
      }
      break;
    }
    case OPT_CAS_EMMG_VERSION: {
      cas_vendor_t *vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-emmg-version");
      p->any_cas_flag = 1;
      if (!vend) return ARGS_ERR;
      if (cas_version_parse(optarg, &vend->emmg_version)) {
        argerr("invalid --cas-emmg-version: %s (2|3)", optarg);
        return ARGS_ERR;
      }
      break;
    }
    case OPT_CAS_EMMG_MAX_CONNS: {
      cas_vendor_t *vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-emmg-max-conns");
      unsigned v;
      p->any_cas_flag = 1;
      if (!vend) return ARGS_ERR;
      if (argutil_uint_range(optarg, 1, EMMG_MAX_CONNS_CEILING, &v)) {
        argerr("invalid --cas-emmg-max-conns: %s (1..%u)", optarg, EMMG_MAX_CONNS_CEILING);
        return ARGS_ERR;
      }
      vend->emmg_max_conns = v;
      break;
    }
    case OPT_CAS_EMM_PID: {
      cas_vendor_t *vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-emm-pid");
      p->any_cas_flag = 1;
      if (!vend) return ARGS_ERR;
      if (rdh_pid_parse(optarg, &vend->emm_pid)) {
        argerr("invalid --cas-emm-pid: %s (0x0001..0x1FFE)", optarg);
        return ARGS_ERR;
      }
      break;
    }
    case OPT_CAS_CP_DURATION: {
      unsigned v;
      p->any_cas_flag = 1;
      if (argutil_uint_range(optarg, 1, 86400000, &v)) {
        argerr("invalid --cas-cp-duration: %s (ms, 1..86400000)", optarg);
        return ARGS_ERR;
      }
      cfg->cas_cp_duration_ms = v;
      break;
    }
    case OPT_CAS_RESILIENCE: {
      static const enum_map_t map[] = {{"frozen", CAS_OUTAGE_FROZEN}, {"cycling", CAS_OUTAGE_CYCLING}, {"silent", CAS_OUTAGE_SILENT}};
      cas_vendor_t *vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-resilience");
      int v;
      p->any_cas_flag = 1;
      if (!vend) return ARGS_ERR;
      if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
        argerr("invalid --cas-resilience: %s (frozen|cycling|silent)", optarg);
        return ARGS_ERR;
      }
      vend->resilience = (cas_outage_mode_t)v;
      break;
    }
    case OPT_CAS_REQUIRED: {
      cas_vendor_t *vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-required");
      p->any_cas_flag = 1;
      if (!vend) return ARGS_ERR;
      vend->required = 1;
      break;
    }
    case OPT_CAS_CWENC_ALGO: {
      cas_vendor_t *vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-cwenc-algo");
      p->any_cas_flag = 1;
      if (!vend) return ARGS_ERR;
      if (strcmp(optarg, "des56") && strcmp(optarg, "aes128") && strcmp(optarg, "aes256")) {
        argerr("invalid --cas-cwenc-algo: %s (des56|aes128|aes256)", optarg);
        return ARGS_ERR;
      }
      bufcpy(vend->cwenc_algorithm, sizeof vend->cwenc_algorithm, optarg);
      break;
    }
    case OPT_CAS_CWENC_AES_MODE: {
      cas_vendor_t *vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-cwenc-aes-mode");
      p->any_cas_flag = 1;
      if (!vend) return ARGS_ERR;
      if (strcmp(optarg, "stream") && strcmp(optarg, "ecb")) {
        argerr("invalid --cas-cwenc-aes-mode: %s (stream|ecb)", optarg);
        return ARGS_ERR;
      }
      bufcpy(vend->cwenc_aes_mode, sizeof vend->cwenc_aes_mode, optarg);
      break;
    }
    case OPT_CAS_CWENC_FIXED_KEY: {
      cas_vendor_t *vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-cwenc-fixed-key");
      p->any_cas_flag = 1;
      if (!vend) return ARGS_ERR;
      if (argutil_bufcpy_opt(TOOL_NAME, vend->cwenc_fixed_key_hex, sizeof vend->cwenc_fixed_key_hex, optarg, "--cas-cwenc-fixed-key"))
        return ARGS_ERR;
      break;
    }
    case OPT_CAS_CWENC_KEY_LIST_A: {
      cas_vendor_t *vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-cwenc-key-list-a");
      p->any_cas_flag = 1;
      if (!vend) return ARGS_ERR;
      if (argutil_bufcpy_opt(TOOL_NAME, vend->cwenc_key_list_a_path, sizeof vend->cwenc_key_list_a_path, optarg, "--cas-cwenc-key-list-a"))
        return ARGS_ERR;
      break;
    }
    case OPT_CAS_CWENC_KEY_LIST_B: {
      cas_vendor_t *vend = rdh_current_cas_vendor(cfg, p->cli_vendors, "cas-cwenc-key-list-b");
      p->any_cas_flag = 1;
      if (!vend) return ARGS_ERR;
      if (argutil_bufcpy_opt(TOOL_NAME, vend->cwenc_key_list_b_path, sizeof vend->cwenc_key_list_b_path, optarg, "--cas-cwenc-key-list-b"))
        return ARGS_ERR;
      break;
    }
    case OPT_CAS_FALLBACK_CLEAR:
      p->any_cas_flag = 1;
      cfg->cas_fallback_clear = 1;
      break;
    case OPT_BISS2_SW:
      if (biss_parse_hex16(optarg, cfg->biss2_sw)) {
        argerr("invalid --biss2-sw: %s (32 hex chars)", optarg);
        return ARGS_ERR;
      }
      cfg->biss2_enabled = 1;
      break;
    case OPT_BISS2_EMIT_ESW:
      if (biss_parse_hex16(optarg, cfg->biss2_esw_id)) {
        argerr("invalid --biss2-emit-esw: %s (32 hex chars)", optarg);
        return ARGS_ERR;
      }
      cfg->biss2_emit_esw = 1;
      break;
    case OPT_BISS1_SW:
      if (biss1_parse_sw(optarg, cfg->biss1_cw)) {
        argerr("invalid --biss1-sw: %s (12 hex chars)", optarg);
        return ARGS_ERR;
      }
      cfg->biss1_enabled = 1;
      break;
    case OPT_BISS2_CA_RECEIVERS:
      cfg->biss2_ca_receivers_dir = optarg;
      cfg->biss2_ca_enabled = 1;
      break;
    case OPT_BISS2_CA_SESSION_ID: {
      char *end;
      unsigned long v = strtoul(optarg, &end, 0);
      if (*end != '\0' || v > 0xFFFFUL) {
        argerr("invalid --biss2-ca-session-id: %s (16 bit, dec or 0x-hex)", optarg);
        return ARGS_ERR;
      }
      cfg->biss2_ca_session_id = (unsigned)v;
      cfg->biss2_ca_session_id_given = 1;
      break;
    }
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
