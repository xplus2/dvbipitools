/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "lib/config/yamlcfg.h"
#include "lib/helper/uriparse.h"
#include "lib/net/netconnect.h"
#include "config.h"
#include "version.h"

void bcg_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->dscp = NET_DSCP_SIGNALLING;
  cfg->window_hours = 24;
  cfg->interval_s = 5;
  cfg->timeout_s = 35;
}

static int set_long(long *dst, const char *v, unsigned min, char *e, size_t n) {
  unsigned u;
  if (yamlcfg_set_uint(&u, v, min, UINT_MAX, e, n)) return -1;
  *dst = (long)u;
  return 0;
}

static int apply_announce(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->fl.have_a, v, e, n);
}

static int apply_listen(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->fl.have_l, v, e, n);
}

static int apply_input(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->input_path, v, e, n);
}

static int apply_map(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->map_path, v, e, n);
}

static int apply_window(void *c, const char *v, char *e, size_t n) {
  return set_long(&((config_t *)c)->window_hours, v, 1, e, n);
}

static int apply_mcast(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (uriparse_mcast_addrport(v, &cfg->family, cfg->mcast_group, sizeof cfg->mcast_group, &cfg->mcast_port)) {
    snprintf(e, n, "invalid group:port '%s'", v);
    return -1;
  }
  cfg->fl.have_mcast = 1;
  return 0;
}

static int apply_iface(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->iface, v, e, n);
}

static int apply_dscp(void *c, const char *v, char *e, size_t n) {
  if (net_dscp_parse(v, &((config_t *)c)->dscp)) {
    snprintf(e, n, "invalid '%s' (video-high|video-low|voice|signalling|best-effort|0..63)", v);
    return -1;
  }
  return 0;
}

static int apply_interval(void *c, const char *v, char *e, size_t n) {
  return set_long(&((config_t *)c)->interval_s, v, 0, e, n);
}

static int apply_timeout(void *c, const char *v, char *e, size_t n) {
  return set_long(&((config_t *)c)->timeout_s, v, 0, e, n);
}

static int apply_output(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->output_path, v, e, n);
}

static int apply_csv_map(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->csvmap_path, v, e, n);
}

static int apply_compress(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->compress, v, e, n);
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
    {"announce", apply_announce, 0, 0},
    {"listen", apply_listen, 0, 0},
    {"input", apply_input, 1, 0},
    {"map", apply_map, 1, 0},
    {"window", apply_window, 0, 0},
    {"mcast", apply_mcast, 0, 0},
    {"iface", apply_iface, 0, 0},
    {"dscp", apply_dscp, 0, 0},
    {"interval", apply_interval, 0, 0},
    {"timeout", apply_timeout, 0, 0},
    {"output", apply_output, 0, 0},
    {"csv-map", apply_csv_map, 0, 0},
    {"compress", apply_compress, 0, 0},
    {"verbose", apply_verbose, 0, 0},
    {"color", apply_color, 0, 0},
    {"daemonize", apply_daemonize, 0, 0},
    {"metrics.sock", apply_metrics_sock, 0, 0},
    {"metrics.id", apply_metrics_id, 0, 0},
    {"metrics.interval", apply_metrics_interval, 0, 0},
};

int bcg_cfg_load(config_t *cfg, const char *path, int strict) {
  yamlcfg_t y;
  return yamlcfg_load(&y, TOOL_NAME, strict ? YAMLCFG_STRICT : 0, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], cfg) == YAMLCFG_ERROR ? -1 : 0;
}

static void warn_if(yamlcfg_t *y, int cond, const char *msg) {
  if (cond) yamlcfg_warn(y, "%s", msg);
}

int bcg_cfg_test(const char *path, int strict) {
  yamlcfg_t y;
  config_t cfg;
  const args_flags_t *fl = &cfg.fl;
  bcg_cfg_defaults(&cfg);
  if (yamlcfg_load(&y, TOOL_NAME, strict ? YAMLCFG_CHECK | YAMLCFG_STRICT : YAMLCFG_CHECK, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], &cfg) != YAMLCFG_LOADED) return -1;
  warn_if(&y, !fl->have_a && !fl->have_l, "neither announce nor listen set (required unless given on the command line)");
  warn_if(&y, !fl->have_mcast, "mcast not set (required unless given on the command line)");
  warn_if(&y, fl->have_a && !fl->have_l && !cfg.input_path, "input not set (required for announce unless given on the command line)");
  warn_if(&y, fl->have_a && !fl->have_l && !cfg.map_path, "map not set (required for announce unless given on the command line)");
  warn_if(&y, fl->have_a && fl->have_l, "announce and listen are mutually exclusive");
  warn_if(&y, fl->have_l && !fl->have_a && (cfg.metrics_id || cfg.compress), "metrics.id and compress are announce-only");
  warn_if(&y, (cfg.metrics_sock || cfg.metrics_interval_s) && !cfg.metrics_id, "metrics.sock and metrics.interval require metrics.id");
  return yamlcfg_report(&y);
}
