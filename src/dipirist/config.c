/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/config/yamlcfg.h"
#include "lib/helper/argutil.h"
#include "lib/helper/ioutil.h"
#include "lib/mux/fec2022.h"
#include "config.h"
#include "version.h"

void rist_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->profile = RIST_PROF_SIMPLE;
}

static int apply_iface(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->iface, v, e, n);
}

static int apply_insecure(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->insecure_tls, v, e, n);
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

static int apply_color(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_color(&((config_t *)c)->color_mode, v, e, n);
}

static int apply_verbose(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->verbose, v, e, n);
}

static int apply_daemonize(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->daemonize, v, e, n);
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

static int set_buf(char *dst, size_t sz, const char *v, char *e, size_t n) {
  if (strlen(v) >= sz) {
    snprintf(e, n, "too long (max %zu)", sz - 1);
    return -1;
  }
  bufcpy(dst, sz, v);
  return 0;
}

static int apply_in(void *c, const char *v, char *e, size_t n) {
  if (rist_cfg_add_endpoint(c, 0, v)) {
    snprintf(e, n, "invalid endpoint '%s'", v);
    return -1;
  }
  return 0;
}

static int apply_out(void *c, const char *v, char *e, size_t n) {
  if (rist_cfg_add_endpoint(c, 1, v)) {
    snprintf(e, n, "invalid endpoint '%s'", v);
    return -1;
  }
  return 0;
}

static int apply_profile(void *c, const char *v, char *e, size_t n) {
  static const enum_map_t map[] = {{"simple", RIST_PROF_SIMPLE}, {"main", RIST_PROF_MAIN}};
  int r;
  if (map_lookup(map, sizeof map / sizeof map[0], v, &r)) {
    snprintf(e, n, "invalid '%s' (simple|main)", v);
    return -1;
  }
  ((config_t *)c)->profile = (rist_profile_sel_t)r;
  return 0;
}

static int apply_secret(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->secret, sizeof cfg->secret, v, e, n);
}

static int apply_cname(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->cname, sizeof cfg->cname, v, e, n);
}

static int apply_buffer(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->buffer_ms, v, 1, 60000, e, n);
}

static const yamlcfg_key_t keys[] = {
    {"in", apply_in, 0, 1},
    {"out", apply_out, 0, 1},
    {"iface", apply_iface, 0, 0},
    {"insecure", apply_insecure, 0, 0},
    {"profile", apply_profile, 0, 0},
    {"secret", apply_secret, 0, 0},
    {"cname", apply_cname, 0, 0},
    {"buffer", apply_buffer, 0, 0},
    {"al-fec", apply_al_fec, 0, 0},
    {"al-fec-port", apply_al_fec_port, 0, 0},
    {"metrics.sock", apply_metrics_sock, 0, 0},
    {"metrics.id", apply_metrics_id, 0, 0},
    {"metrics.interval", apply_metrics_interval, 0, 0},
    {"color", apply_color, 0, 0},
    {"verbose", apply_verbose, 0, 0},
    {"daemonize", apply_daemonize, 0, 0},
};

int rist_cfg_load(config_t *cfg, const char *path, int strict) {
  yamlcfg_t y;
  int mode = strict ? YAMLCFG_STRICT : 0;
  int rc = yamlcfg_load(&y, TOOL_NAME, mode, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], cfg);
  return rc == YAMLCFG_ERROR ? -1 : 0;
}

static void warn_if(yamlcfg_t *y, int cond, const char *msg) {
  if (cond) yamlcfg_warn(y, "%s", msg);
}

int rist_cfg_test(const char *path, int strict) {
  yamlcfg_t y;
  config_t cfg;

  rist_cfg_defaults(&cfg);
  if (yamlcfg_load(&y, TOOL_NAME, strict ? YAMLCFG_CHECK | YAMLCFG_STRICT : YAMLCFG_CHECK, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], &cfg) != YAMLCFG_LOADED) return -1;
  warn_if(&y, !cfg.n_in, "in not set (required unless given on the command line)");
  warn_if(&y, !cfg.n_out, "out not set (required unless given on the command line)");
  warn_if(&y, cfg.n_in && cfg.n_out && cfg.in.is_rist == cfg.out.is_rist, "exactly one of in/out must be rist://, the other a plain endpoint");
  warn_if(&y, cfg.secret[0] && cfg.profile != RIST_PROF_MAIN, "secret requires profile main");
  warn_if(&y, (cfg.metrics_sock || cfg.metrics_interval_s) && !cfg.metrics_id, "metrics.sock and metrics.interval require metrics.id");
  warn_if(&y, cfg.al_fec_l && !cfg.al_fec_port, "al-fec requires al-fec-port");
  warn_if(&y, !cfg.al_fec_l && cfg.al_fec_port, "al-fec-port has no effect without al-fec");
  return yamlcfg_report(&y);
}
