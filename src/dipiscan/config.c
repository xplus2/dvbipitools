/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "lib/config/yamlcfg.h"
#include "config.h"
#include "version.h"

int scan_cfg_default_exists(void) {
  return access(DEFAULT_CONFIG_PATH, R_OK) == 0;
}

void scan_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  scan_cfg_mcast(cfg, "239.19.75.0");
  cfg->port_lo = cfg->port_hi = 8700;
  cfg->format = OUT_M3U;
  cfg->timeout_ms = 1000;
  cfg->jets = 1;
}

static int apply_mcast(void *c, const char *v, char *e, size_t n) {
  if (scan_cfg_mcast(c, v)) {
    snprintf(e, n, "invalid '%s' (addr, addr/prefixlen, or startaddr-stopaddr)", v);
    return -1;
  }
  return 0;
}

static int apply_port(void *c, const char *v, char *e, size_t n) {
  if (scan_cfg_port(c, v)) {
    snprintf(e, n, "invalid '%s' (port or port-port)", v);
    return -1;
  }
  return 0;
}

static int apply_format(void *c, const char *v, char *e, size_t n) {
  if (scan_cfg_format(c, v)) {
    snprintf(e, n, "invalid '%s' (m3u|csv|xspf|xml|null)", v);
    return -1;
  }
  return 0;
}

static int apply_provider(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->provider, v, e, n);
}

static int apply_out(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->out_path, v, e, n);
}

static int apply_timeout(void *c, const char *v, char *e, size_t n) {
  unsigned u;
  if (yamlcfg_set_uint(&u, v, 1, 3600, e, n)) return -1;
  ((config_t *)c)->timeout_ms = (int)(u * 1000);
  return 0;
}

static int apply_jets(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->jets, v, 1, DIPISCAN_MAX_JETS, e, n);
}

static int apply_mpts(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->mpts, v, e, n);
}

static int apply_http_proxy(void *c, const char *v, char *e, size_t n) {
  if (scan_cfg_http_proxy(c, v)) {
    snprintf(e, n, "invalid '%s' (host[:port])", v);
    return -1;
  }
  return 0;
}

static int apply_http_path(void *c, const char *v, char *e, size_t n) {
  if (scan_cfg_http_path(v)) {
    snprintf(e, n, "invalid '%s' (%%g, %%p, %%%% only)", v);
    return -1;
  }
  return yamlcfg_set_str(&((config_t *)c)->http_path_tmpl, v, e, n);
}

static int apply_iface(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->iface, v, e, n);
}

static int apply_verbose(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->verbose, v, e, n);
}

static int apply_color(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_color(&((config_t *)c)->color_mode, v, e, n);
}

static const yamlcfg_key_t keys[] = {
  {"mcast", apply_mcast, 0, 0},
  {"port", apply_port, 0, 0},
  {"format", apply_format, 0, 0},
  {"provider", apply_provider, 0, 0},
  {"out", apply_out, 0, 0},
  {"timeout", apply_timeout, 0, 0},
  {"jets", apply_jets, 0, 0},
  {"mpts", apply_mpts, 0, 0},
  {"http-proxy", apply_http_proxy, 0, 0},
  {"http-path", apply_http_path, 0, 0},
  {"iface", apply_iface, 0, 0},
  {"verbose", apply_verbose, 0, 0},
  {"color", apply_color, 0, 0},
};

int scan_cfg_load(config_t *cfg, const char *path) {
  yamlcfg_t y;
  return yamlcfg_load(&y, TOOL_NAME, 0, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof *keys, cfg) == YAMLCFG_ERROR ? -1 : 0;
}

static void warn_if(yamlcfg_t *y, int cond, const char *msg) {
  if (cond) yamlcfg_warn(y, "%s", msg);
}

int scan_cfg_test(const char *path) {
  yamlcfg_t y;
  config_t cfg;

  scan_cfg_defaults(&cfg);
  if (yamlcfg_load(&y, TOOL_NAME, 1, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof *keys, &cfg) != YAMLCFG_LOADED) return -1;
  warn_if(&y, cfg.format == OUT_XML && !cfg.provider, "format xml requires provider (unless given on the command line)");
  warn_if(&y, cfg.http_path_tmpl && !cfg.http_proxy, "http-path needs http-proxy");
  yamlcfg_report(&y);
  return 0;
}
