/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/config/yamlcfg.h"
#include "lib/helper/argutil.h"
#include "lib/helper/ioutil.h"
#include "config.h"
#include "version.h"

#define AUTH_BUF 256

static char auth_buf[AUTH_BUF];

void cam378_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->port = ARGS_DEFAULT_PORT;
  cfg->password = ARGS_DEFAULT_PASSWORD;
  cfg->cw_len = 16;
}

int cam378_cfg_caid(const char *p, unsigned *out) {
  char *end;
  unsigned long v;
  if (*p == '\0') return -1;
  v = strtoul(p, &end, 16);
  if (*end != '\0' || v == 0 || v > 0xFFFF) return -1;
  *out = (unsigned)v;
  return 0;
}

static int apply_key(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->key_path, v, e, n);
}

static int apply_serial(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->serial, v, e, n);
}

static int apply_port(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->port, v, 1, 65535, e, n);
}

static int apply_auth(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  char *colon;
  if (!*v || strlen(v) >= sizeof auth_buf) {
    snprintf(e, n, "empty or too long");
    return -1;
  }
  bufcpy(auth_buf, sizeof auth_buf, v);
  colon = strchr(auth_buf, ':');
  if (colon) {
    *colon = '\0';
    cfg->username = auth_buf;
    cfg->password = colon + 1;
  } else {
    cfg->password = auth_buf;
  }
  return 0;
}

static int apply_caid(void *c, const char *v, char *e, size_t n) {
  if (cam378_cfg_caid(v, &((config_t *)c)->caid)) {
    snprintf(e, n, "invalid '%s' (hex, 1..FFFF)", v);
    return -1;
  }
  return 0;
}

static int apply_algo(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (!strcmp(v, "csa2"))       cfg->cw_len = 8;
  else if (!strcmp(v, "cissa")) cfg->cw_len = 16;
  else {
    snprintf(e, n, "invalid '%s' (cissa|csa2)", v);
    return -1;
  }
  return 0;
}

static int apply_verbose(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->verbose, v, e, n);
}

static int apply_color(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_color(&((config_t *)c)->color_mode, v, e, n);
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

static const yamlcfg_key_t keys[] = {
    {"key", apply_key, 1, 0},
    {"serial", apply_serial, 0, 0},
    {"port", apply_port, 0, 0},
    {"auth", apply_auth, 0, 0},
    {"caid", apply_caid, 0, 0},
    {"algo", apply_algo, 0, 0},
    {"verbose", apply_verbose, 0, 0},
    {"color", apply_color, 0, 0},
    {"metrics", apply_metrics_sock, 0, 0},
    {"metrics.id", apply_metrics_id, 0, 0},
    {"metrics.interval", apply_metrics_interval, 0, 0},
    {"daemonize", apply_daemonize, 0, 0},
};

int cam378_cfg_load(config_t *cfg, const char *path) {
  yamlcfg_t y;
  return yamlcfg_load(&y, TOOL_NAME, 0, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof *keys, cfg) == YAMLCFG_ERROR ? -1 : 0;
}

static void warn_if(yamlcfg_t *y, int cond, const char *msg) {
  if (cond) yamlcfg_warn(y, "%s", msg);
}

int cam378_cfg_test(const char *path) {
  yamlcfg_t y;
  config_t cfg;
  cam378_cfg_defaults(&cfg);
  if (yamlcfg_load(&y, TOOL_NAME, 1, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof *keys, &cfg) != YAMLCFG_LOADED) return -1;
  warn_if(&y, !cfg.key_path, "key not set (required unless given on the command line)");
  warn_if(&y, (cfg.metrics_sock || cfg.metrics_interval_s) && !cfg.metrics_id, "metrics and metrics-interval require metrics-id");
  yamlcfg_report(&y);
  return 0;
}
