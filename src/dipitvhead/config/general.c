/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>

#include "lib/config/yamlcfg.h"
#include "lib/helper/argutil.h"
#include "lib/helper/ioutil.h"
#include "../config.h"
#include "priv.h"

int tvh_apply_nit(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return tvh_set_table(&cfg->nit_mode, cfg->nit_text, sizeof cfg->nit_text, v, e, n);
}

int tvh_apply_default_provider(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return tvh_set_buf(cfg->default_provider_text, sizeof cfg->default_provider_text, v, e, n);
}

int tvh_apply_bitrate(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->bitrate_kbps, v, 1, 1000000, e, n);
}

int tvh_apply_stuff(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->stuff, v, e, n);
}

int tvh_apply_burst_limit(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->burst_limit, v, e, n);
}

int tvh_apply_error(void *c, const char *v, char *e, size_t n) {
  unsigned u;
  if (yamlcfg_set_uint(&u, v, 0, UINT_MAX, e, n)) return -1;
  ((config_t *)c)->error_retry_s = (long)u;
  return 0;
}

int tvh_apply_insecure(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->insecure_tls, v, e, n);
}

int tvh_apply_tsid(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->tsid, v, 1, 0xFFFF, e, n);
}

int tvh_apply_onid(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->onid, v, 1, 0xFFFF, e, n);
}

int tvh_apply_verbose(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->verbose, v, e, n);
}

int tvh_apply_daemonize(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->daemonize, v, e, n);
}

int tvh_apply_color(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_color(&((config_t *)c)->color_mode, v, e, n);
}

int tvh_apply_metrics_sock(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->metrics_sock, v, e, n);
}

int tvh_apply_metrics_id(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->metrics_id, v, e, n);
}

int tvh_apply_metrics_interval(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->metrics_interval_s, v, 1, 86400, e, n);
}

int tvh_apply_metrics_known_pids(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (metrics_known_pids_parse(v, cfg->metrics_known_pids, &cfg->metrics_n_known_pids)) {
    bufcpy(e, n, "must be comma separated pids, 0..8191");
    return -1;
  }
  return 0;
}

int tvh_apply_metrics_inspect_ts(void *c, const char *v, char *e, size_t n) {
  if (metrics_inspect_ts_parse(v, &((config_t *)c)->metrics_inspect_ts)) {
    bufcpy(e, n, "must be off|basic|medium|full");
    return -1;
  }
  return 0;
}
