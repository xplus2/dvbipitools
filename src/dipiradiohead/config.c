/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "lib/config/yamlcfg.h"
#include "lib/helper/argutil.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/uriparse.h"
#include "lib/mux/fec2022.h"
#include "lib/net/netconnect.h"
#include "config.h"
#include "version.h"

static struct {
  int input;
  int have_input;
  int vendor;
  int have_vendor;
} item;

void rdh_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  memset(&item, 0, sizeof item);
  cfg->tsid = 1;
  cfg->onid = 1;
  cfg->dscp = NET_DSCP_VIDEO_HIGH;
  cfg->cas_cp_duration_ms = 10000;
}

static int set_buf(char *dst, size_t sz, const char *v, char *e, size_t n) {
  if (strlen(v) >= sz) {
    snprintf(e, n, "too long (max %zu)", sz - 1);
    return -1;
  }
  bufcpy(dst, sz, v);
  return 0;
}

static int set_pbkeylen(int *dst, const char *v, char *e, size_t n) {
  unsigned u;
  if (argutil_uint_range(v, 16, 32, &u) || (u != 16 && u != 24 && u != 32)) {
    snprintf(e, n, "invalid '%s' (16|24|32)", v);
    return -1;
  }
  *dst = (int)u;
  return 0;
}

static int add_input(config_t *cfg, const char *uri, char *e, size_t n) {
  if (cfg->n_inputs >= RADIOHEAD_MAX_INPUTS) {
    snprintf(e, n, "too many inputs (max %d)", RADIOHEAD_MAX_INPUTS);
    return -1;
  }
  memset(&cfg->inputs[cfg->n_inputs], 0, sizeof cfg->inputs[0]);
  cfg->inputs[cfg->n_inputs].uri = uri;
  cfg->n_inputs++;
  return 0;
}

static int add_peer(config_t *cfg, const char *uri, char *e, size_t n) {
  if (strncmp(uri, "rist://", 7) == 0) {
    if (cfg->n_srt > 0) {
      snprintf(e, n, "rist:// and srt:// peers cannot mix in one run");
      return -1;
    }
    if (cfg->n_rist >= ARGS_MAX_RIST_PEERS) {
      snprintf(e, n, "too many peers (max %d)", ARGS_MAX_RIST_PEERS);
      return -1;
    }
    if (set_buf(cfg->rist_uri[cfg->n_rist], sizeof cfg->rist_uri[0], uri, e, n)) return -1;
    cfg->n_rist++;
    return 0;
  }
  if (strncmp(uri, "srt://", 6) == 0) {
    if (cfg->n_rist > 0) {
      snprintf(e, n, "rist:// and srt:// peers cannot mix in one run");
      return -1;
    }
    if (cfg->n_srt >= ARGS_MAX_SRT_PEERS) {
      snprintf(e, n, "too many srt:// peers (max %d)", ARGS_MAX_SRT_PEERS);
      return -1;
    }
    if (uri[6] == '@') {
      snprintf(e, n, "srt:// output always calls out, no listener mode");
      return -1;
    }
    if (argutil_addrport_parse(uri + 6, &cfg->srt_family[cfg->n_srt], cfg->srt_host[cfg->n_srt], sizeof cfg->srt_host[0], &cfg->srt_port[cfg->n_srt])) {
      snprintf(e, n, "invalid srt uri '%s'", uri);
      return -1;
    }
    cfg->n_srt++;
    return 0;
  }
  snprintf(e, n, "invalid uri '%s' (must start with rist:// or srt://)", uri);
  return -1;
}

static int item_hook(void *c, const char *list, int begin, char *e, size_t n) {
  (void)c;
  if (!strcmp(list, "input")) {
    item.input = begin;
    if (begin) {
      item.have_input = 0;
    } else if (!item.have_input) {
      snprintf(e, n, "missing input");
      return -1;
    }
  } else if (!strcmp(list, "cas.ecmg")) {
    item.vendor = begin;
    if (begin) {
      item.have_vendor = 0;
    } else if (!item.have_vendor) {
      snprintf(e, n, "missing ecmg");
      return -1;
    }
  }
  return 0;
}

static radio_input_t *cur_input(config_t *cfg, char *e, size_t n) {
  if (!item.input) {
    snprintf(e, n, "only valid inside an input list item");
    return NULL;
  }
  return &cfg->inputs[cfg->n_inputs - 1];
}

static cas_vendor_t *cur_vendor(config_t *cfg, char *e, size_t n) {
  if (!item.vendor) {
    snprintf(e, n, "only valid inside a cas.ecmg list item");
    return NULL;
  }
  return &cfg->cas_vendors[cfg->n_cas_vendors - 1];
}

static int apply_input(void *c, const char *v, char *e, size_t n) {
  const char *uri;
  if (yamlcfg_set_str(&uri, v, e, n)) return -1;
  if (add_input(c, uri, e, n)) return -1;
  if (item.input) item.have_input = 1;
  return 0;
}

static int apply_input_sid(void *c, const char *v, char *e, size_t n) {
  radio_input_t *in = cur_input(c, e, n);
  return in ? yamlcfg_set_uint(&in->sid, v, 1, 0xFFFF, e, n) : -1;
}

static int apply_input_sdt(void *c, const char *v, char *e, size_t n) {
  radio_input_t *in = cur_input(c, e, n);
  return in ? set_buf(in->sdt_text, sizeof in->sdt_text, v, e, n) : -1;
}

static int apply_input_provider(void *c, const char *v, char *e, size_t n) {
  radio_input_t *in = cur_input(c, e, n);
  return in ? set_buf(in->provider_text, sizeof in->provider_text, v, e, n) : -1;
}

static int apply_mcast(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (uriparse_mcast_addrport(v, &cfg->family, cfg->mcast_group, sizeof cfg->mcast_group, &cfg->mcast_port)) {
    snprintf(e, n, "invalid '%s' (multicast addr:port)", v);
    return -1;
  }
  return 0;
}

static int apply_out_iface(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->iface, v, e, n);
}

static int apply_rtp(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->rtp, v, e, n);
}

static int apply_ttl(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->ttl, v, 1, 255, e, n);
}

static int apply_dscp(void *c, const char *v, char *e, size_t n) {
  if (net_dscp_parse(v, &((config_t *)c)->dscp)) {
    snprintf(e, n, "invalid '%s' (video-high|video-low|voice|signalling|best-effort|0..63)", v);
    return -1;
  }
  return 0;
}

static int apply_al_fec(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (fec2022_parse_ld(v, &cfg->al_fec_l, &cfg->al_fec_d)) {
    snprintf(e, n, "invalid '%s' (want L:D, L*D<=400, L<=40)", v);
    return -1;
  }
  return 0;
}

static int apply_al_fec_port(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->al_fec_port, v, 1, 65535, e, n);
}

static int apply_nit(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->nit_text, sizeof cfg->nit_text, v, e, n);
}

static int apply_default_provider(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->default_provider_text, sizeof cfg->default_provider_text, v, e, n);
}

static int apply_rist(void *c, const char *v, char *e, size_t n) {
  return add_peer(c, v, e, n);
}

static int apply_profile(void *c, const char *v, char *e, size_t n) {
  static const enum_map_t map[] = {{"simple", RIST_PROF_SIMPLE}, {"main", RIST_PROF_MAIN}};
  config_t *cfg = c;
  int m;
  if (map_lookup(map, sizeof map / sizeof map[0], v, &m)) {
    snprintf(e, n, "invalid '%s' (simple|main)", v);
    return -1;
  }
  cfg->rist_profile = (rist_profile_sel_t)m;
  cfg->rist_profile_given = 1;
  return 0;
}

static int apply_secret(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->rist_secret, sizeof cfg->rist_secret, v, e, n);
}

static int apply_cname(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->rist_cname, sizeof cfg->rist_cname, v, e, n);
}

static int apply_buffer(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->rist_buffer_ms, v, 1, UINT_MAX, e, n);
}

static int apply_srt_group_mode(void *c, const char *v, char *e, size_t n) {
  static const enum_map_t map[] = {{"broadcast", SRT_BOND_BROADCAST}, {"backup", SRT_BOND_BACKUP}};
  int m;
  if (map_lookup(map, sizeof map / sizeof map[0], v, &m)) {
    snprintf(e, n, "invalid '%s' (broadcast|backup)", v);
    return -1;
  }
  ((config_t *)c)->srt_group_mode = (srt_bond_mode_t)m;
  return 0;
}

static int apply_srt_passphrase(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->srt_passphrase, sizeof cfg->srt_passphrase, v, e, n);
}

static int apply_srt_pbkeylen(void *c, const char *v, char *e, size_t n) {
  return set_pbkeylen(&((config_t *)c)->srt_pbkeylen, v, e, n);
}

static int apply_srt_streamid(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->srt_streamid, sizeof cfg->srt_streamid, v, e, n);
}

static int apply_srt_packetfilter(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->srt_packetfilter, sizeof cfg->srt_packetfilter, v, e, n);
}

static int apply_srt_latency(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->srt_latency_ms, v, 1, 60000, e, n);
}

static int apply_error(void *c, const char *v, char *e, size_t n) {
  unsigned u;
  if (yamlcfg_set_uint(&u, v, 0, UINT_MAX, e, n)) return -1;
  ((config_t *)c)->error_retry_s = (long)u;
  return 0;
}

static int apply_insecure(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->insecure_tls, v, e, n);
}

static int apply_tsid(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->tsid, v, 1, 0xFFFF, e, n);
}

static int apply_onid(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->onid, v, 1, 0xFFFF, e, n);
}

static int apply_verbose(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->verbose, v, e, n);
}

static int apply_daemonize(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->daemonize, v, e, n);
}

static int apply_color(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_color(&((config_t *)c)->color_mode, v, e, n);
}

static int apply_metrics_sock(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->metrics_sock, v, e, n);
}

static int apply_metrics_id(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->metrics_id, v, e, n);
}

static int apply_metrics_interval(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->metrics_interval_s, v, 1, 86400, e, n);
}

static int apply_cas_algo(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  cfg->any_cas_flag = 1;
  return cas_set_algo(&cfg->cas_algo, v, e, n);
}

static int apply_cas_cp_duration(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  cfg->any_cas_flag = 1;
  return cas_set_cp_duration(&cfg->cas_cp_duration_ms, v, e, n);
}

static int apply_cas_fallback_clear(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  cfg->any_cas_flag = 1;
  return yamlcfg_set_bool(&cfg->cas_fallback_clear, v, e, n);
}

static int apply_cas_ecmg(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  cfg->any_cas_flag = 1;
  if (cas_vendor_add(cfg->cas_vendors, &cfg->n_cas_vendors, v, e, n)) return -1;
  if (item.vendor) item.have_vendor = 1;
  return 0;
}

static int apply_cas_required(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  cas_vendor_t *vd = cur_vendor(cfg, e, n);
  cfg->any_cas_flag = 1;
  return vd ? yamlcfg_set_bool(&vd->required, v, e, n) : -1;
}

#define VENDOR_KEY(name, setter) \
  static int apply_cas_##name(void *c, const char *v, char *e, size_t n) { \
    config_t *cfg = c; \
    cas_vendor_t *vd = cur_vendor(cfg, e, n); \
    cfg->any_cas_flag = 1; \
    return vd ? setter(vd, v, e, n) : -1; \
  }

VENDOR_KEY(ecmg_version, cas_vendor_set_ecmg_version)
VENDOR_KEY(super_id, cas_vendor_set_super_id)
VENDOR_KEY(ecm_id, cas_vendor_set_ecm_id)
VENDOR_KEY(ecm_pid, cas_vendor_set_ecm_pid)
VENDOR_KEY(emmg_port, cas_vendor_set_emmg_port)
VENDOR_KEY(emmg_max_conns, cas_vendor_set_emmg_max_conns)
VENDOR_KEY(emmg_version, cas_vendor_set_emmg_version)
VENDOR_KEY(emmg_reverse, cas_vendor_set_emmg_reverse)
VENDOR_KEY(emm_pid, cas_vendor_set_emm_pid)
VENDOR_KEY(resilience, cas_vendor_set_resilience)
VENDOR_KEY(cwenc_algo, cas_vendor_set_cwenc_algo)
VENDOR_KEY(cwenc_aes_mode, cas_vendor_set_cwenc_aes_mode)
VENDOR_KEY(cwenc_fixed_key, cas_vendor_set_cwenc_fixed_key)
VENDOR_KEY(cwenc_key_list_a, cas_vendor_set_cwenc_key_list_a)
VENDOR_KEY(cwenc_key_list_b, cas_vendor_set_cwenc_key_list_b)

static int apply_biss1_sw(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return cas_set_biss1_sw(&cfg->biss1_enabled, cfg->biss1_cw, v, e, n);
}

static int apply_biss2_sw(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return cas_set_biss2_sw(&cfg->biss2_enabled, cfg->biss2_sw, v, e, n);
}

static int apply_biss2_emit_esw(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return cas_set_biss2_emit_esw(&cfg->biss2_emit_esw, cfg->biss2_esw_id, v, e, n);
}

static int apply_biss2_ca_receivers(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_str(&cfg->biss2_ca_receivers_dir, v, e, n)) return -1;
  cfg->biss2_ca_enabled = 1;
  return 0;
}

static int apply_biss2_ca_session_id(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return cas_set_biss2_ca_session_id(&cfg->biss2_ca_session_id, &cfg->biss2_ca_session_id_given, v, e, n);
}

static const yamlcfg_key_t keys[] = {
{"input", apply_input, 0, YAMLCFG_LIST_KEYED},
{"input.sid", apply_input_sid, 0, 0},
{"input.sdt", apply_input_sdt, 0, 0},
{"input.provider", apply_input_provider, 0, 0},
{"mcast", apply_mcast, 0, 0},
{"out-iface", apply_out_iface, 0, 0},
{"rtp", apply_rtp, 0, 0},
{"ttl", apply_ttl, 0, 0},
{"dscp", apply_dscp, 0, 0},
{"al-fec", apply_al_fec, 0, 0},
{"al-fec-port", apply_al_fec_port, 0, 0},
{"nit", apply_nit, 0, 0},
{"default-provider", apply_default_provider, 0, 0},
{"remote", apply_rist, 0, 1},
{"rist.profile", apply_profile, 0, 0},
{"rist.secret", apply_secret, 0, 0},
{"rist.cname", apply_cname, 0, 0},
{"rist.buffer", apply_buffer, 0, 0},
{"srt.group-mode", apply_srt_group_mode, 0, 0},
{"srt.passphrase", apply_srt_passphrase, 0, 0},
{"srt.pbkeylen", apply_srt_pbkeylen, 0, 0},
{"srt.streamid", apply_srt_streamid, 0, 0},
{"srt.packetfilter", apply_srt_packetfilter, 0, 0},
{"srt.latency", apply_srt_latency, 0, 0},
{"error", apply_error, 0, 0},
{"insecure", apply_insecure, 0, 0},
{"tsid", apply_tsid, 0, 0},
{"onid", apply_onid, 0, 0},
{"verbose", apply_verbose, 0, 0},
{"daemonize", apply_daemonize, 0, 0},
{"color", apply_color, 0, 0},
{"metrics.sock", apply_metrics_sock, 0, 0},
{"metrics.id", apply_metrics_id, 0, 0},
{"metrics.interval", apply_metrics_interval, 0, 0},
{"cas.algo", apply_cas_algo, 0, 0},
{"cas.cp-duration", apply_cas_cp_duration, 0, 0},
{"cas.fallback-clear", apply_cas_fallback_clear, 0, 0},
{"cas.ecmg", apply_cas_ecmg, 0, YAMLCFG_LIST_KEYED},
{"cas.ecmg.ecmg-version", apply_cas_ecmg_version, 0, 0},
{"cas.ecmg.super-id", apply_cas_super_id, 0, 0},
{"cas.ecmg.ecm-id", apply_cas_ecm_id, 0, 0},
{"cas.ecmg.ecm-pid", apply_cas_ecm_pid, 0, 0},
{"cas.ecmg.emmg-port", apply_cas_emmg_port, 0, 0},
{"cas.ecmg.emmg-max-conns", apply_cas_emmg_max_conns, 0, 0},
{"cas.ecmg.emmg-version", apply_cas_emmg_version, 0, 0},
{"cas.ecmg.emmg-reverse", apply_cas_emmg_reverse, 0, 0},
{"cas.ecmg.emm-pid", apply_cas_emm_pid, 0, 0},
{"cas.ecmg.resilience", apply_cas_resilience, 0, 0},
{"cas.ecmg.required", apply_cas_required, 0, 0},
{"cas.ecmg.cwenc-algo", apply_cas_cwenc_algo, 0, 0},
{"cas.ecmg.cwenc-aes-mode", apply_cas_cwenc_aes_mode, 0, 0},
{"cas.ecmg.cwenc-fixed-key", apply_cas_cwenc_fixed_key, 0, 0},
{"cas.ecmg.cwenc-key-list-a", apply_cas_cwenc_key_list_a, 1, 0},
{"cas.ecmg.cwenc-key-list-b", apply_cas_cwenc_key_list_b, 1, 0},
{"biss1.sw", apply_biss1_sw, 0, 0},
{"biss2.sw", apply_biss2_sw, 0, 0},
{"biss2.emit-esw", apply_biss2_emit_esw, 0, 0},
{"biss2.ca-receivers", apply_biss2_ca_receivers, 1, 0},
{"biss2.ca-session-id", apply_biss2_ca_session_id, 0, 0},
};

int rdh_cfg_load(config_t *cfg, const char *path, int strict) {
  yamlcfg_t y;
  return yamlcfg_load_items(&y, TOOL_NAME, strict ? YAMLCFG_STRICT : 0, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], cfg, item_hook) == YAMLCFG_ERROR ? -1 : 0;
}

static void warn_if(yamlcfg_t *y, int cond, const char *msg) {
  if (cond) yamlcfg_warn(y, "%s", msg);
}

int rdh_cfg_test(const char *path, int strict) {
  yamlcfg_t y;
  config_t cfg;
  size_t pwlen;

  rdh_cfg_defaults(&cfg);
  if (yamlcfg_load_items(&y, TOOL_NAME, strict ? YAMLCFG_CHECK | YAMLCFG_STRICT : YAMLCFG_CHECK, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], &cfg, item_hook) != YAMLCFG_LOADED) return -1;
  pwlen = strlen(cfg.srt_passphrase);

  warn_if(&y, cfg.n_inputs == 0, "input not set (required unless given on the command line)");
  warn_if(&y, !cfg.mcast_port && cfg.n_rist == 0 && cfg.n_srt == 0, "mcast or rist not set (one is required unless given on the command line)");
  warn_if(&y, (cfg.metrics_sock || cfg.metrics_interval_s) && !cfg.metrics_id, "metrics.sock and metrics.interval require metrics.id");
  warn_if(&y, cfg.al_fec_l && !cfg.al_fec_port, "al-fec requires al-fec-port");
  warn_if(&y, !cfg.al_fec_l && cfg.al_fec_port, "al-fec-port has no effect without al-fec");
  warn_if(&y, cfg.al_fec_l && !cfg.rtp, "al-fec requires rtp");
  warn_if(&y, cfg.al_fec_l && !cfg.mcast_port, "al-fec has no effect without mcast");
  warn_if(&y, cfg.n_rist == 0 && (cfg.rist_profile_given || cfg.rist_secret[0] || cfg.rist_cname[0] || cfg.rist_buffer_ms), "profile/secret/cname/buffer have no effect without rist");
  warn_if(&y, cfg.n_rist > 0 && cfg.rist_secret[0] && cfg.rist_profile != RIST_PROF_MAIN, "rist-secret requires rist-profile main");
  warn_if(&y, cfg.n_srt > 1 && cfg.srt_group_mode == SRT_BOND_NONE, "bonding several srt:// peers requires srt.group-mode");
  warn_if(&y, cfg.n_srt == 1 && cfg.srt_group_mode != SRT_BOND_NONE, "srt.group-mode has no effect with a single srt:// peer");
  warn_if(&y, cfg.n_srt == 0 && (cfg.srt_group_mode != SRT_BOND_NONE || cfg.srt_passphrase[0] || cfg.srt_pbkeylen || cfg.srt_streamid[0] || cfg.srt_packetfilter[0] || cfg.srt_latency_ms), "srt.* settings need an srt:// peer");
  warn_if(&y, pwlen && (pwlen < 10 || pwlen > 79), "srt.passphrase must be 10..79 characters");
  warn_if(&y, cfg.srt_pbkeylen && !pwlen, "srt.pbkeylen requires srt.passphrase");
  warn_if(&y, cfg.any_cas_flag && cfg.cas_algo == CAS_ALGO_NONE, "cas.* options require cas.algo");
  if (cas_args_validate(TOOL_NAME, cfg.cas_algo, cfg.cas_vendors, cfg.n_cas_vendors, cfg.biss2_enabled, cfg.biss1_enabled, cfg.biss2_ca_enabled, cfg.biss2_emit_esw, cfg.biss2_ca_session_id_given, cfg.cas_cp_duration_ms) != 0)
    y.warnings++;
  return yamlcfg_report(&y);
}
