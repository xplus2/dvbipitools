/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>

#include "lib/config/yamlcfg.h"
#include "lib/helper/ioutil.h"
#include "../config.h"
#include "priv.h"

int tvh_apply_cas_algo(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  cfg->any_cas_flag = 1;
  return cas_set_algo(&cfg->cas_algo, v, e, n);
}

int tvh_apply_cas_pids(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  cfg->any_cas_flag = 1;
  if (tvh_cfg_cas_pids(cfg, v)) {
    snprintf(e, n, "invalid '%s' (pids and/or video|audio|lcevc)", v);
    return -1;
  }
  return 0;
}

int tvh_apply_cas_cp_duration(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  cfg->any_cas_flag = 1;
  return cas_set_cp_duration(&cfg->cas_cp_duration_ms, v, e, n);
}

int tvh_apply_cas_fallback_clear(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  cfg->any_cas_flag = 1;
  return yamlcfg_set_bool(&cfg->cas_fallback_clear, v, e, n);
}

int tvh_apply_cas_ecmg(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  cfg->any_cas_flag = 1;
  if (cas_vendor_add(cfg->cas_vendors, &cfg->n_cas_vendors, v, e, n)) return -1;
  if (tvh_item.vendor) tvh_item.have_vendor = 1;
  return 0;
}

int tvh_apply_cas_required(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  cas_vendor_t *vd = tvh_cur_vendor(cfg, e, n);
  cfg->any_cas_flag = 1;
  return vd ? yamlcfg_set_bool(&vd->required, v, e, n) : -1;
}

#define VENDOR_KEY(name, setter) \
  int tvh_apply_cas_##name(void *c, const char *v, char *e, size_t n) { \
    config_t *cfg = c; \
    cas_vendor_t *vd = tvh_cur_vendor(cfg, e, n); \
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

int tvh_apply_biss1_sw(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return cas_set_biss1_sw(&cfg->biss1_enabled, cfg->biss1_cw, v, e, n);
}

int tvh_apply_biss2_sw(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return cas_set_biss2_sw(&cfg->biss2_enabled, cfg->biss2_sw, v, e, n);
}

int tvh_apply_biss2_emit_esw(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return cas_set_biss2_emit_esw(&cfg->biss2_emit_esw, cfg->biss2_esw_id, v, e, n);
}

int tvh_apply_biss2_ca_receivers(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_str(&cfg->biss2_ca_receivers_dir, v, e, n)) return -1;
  cfg->biss2_ca_enabled = 1;
  return 0;
}

int tvh_apply_biss2_ca_session_id(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return cas_set_biss2_ca_session_id(&cfg->biss2_ca_session_id, &cfg->biss2_ca_session_id_given, v, e, n);
}
