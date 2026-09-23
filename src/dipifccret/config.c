/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "lib/config/yamlcfg.h"
#include "lib/helper/ioutil.h"
#include "config.h"
#include "version.h"

void fccret_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->buffer_ms = 2000;
  cfg->rtx_pt = 99;
  cfg->gop_cap_ms = 8000;
  cfg->max_bursts = 4096;
  cfg->burst_multiplier = 1.5;
  cfg->duration_cap_ms = 10000;
  cfg->max_buffer_fill_bound_ms = 30000;
  cfg->congestion_nack_threshold = 5;
  cfg->channel_idle_timeout_s = 120;
  cfg->max_ret_clients = 16384;
  cfg->ret_client_idle_timeout_s = 300;
  cfg->rsi_interval_s = 5;
}

static int set_size(size_t *dst, const char *v, unsigned min, char *e, size_t n) {
  unsigned u;
  if (yamlcfg_set_uint(&u, v, min, UINT_MAX, e, n)) return -1;
  *dst = (size_t)u;
  return 0;
}

static int apply_range(void *c, const char *v, char *e, size_t n) {
  if (fccret_cfg_range(c, v)) {
    snprintf(e, n, "invalid '%s' (comma list of addr/prefix)", v);
    return -1;
  }
  return 0;
}

static int apply_listen(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_addrport(&cfg->listen_family, cfg->listen_addr, sizeof cfg->listen_addr, &cfg->listen_port, v, e, n);
}

static int apply_iface(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->iface, v, e, n);
}

static int apply_max_channels(void *c, const char *v, char *e, size_t n) {
  return set_size(&((config_t *)c)->max_channels, v, 0, e, n);
}

static int apply_channel_idle_timeout(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->channel_idle_timeout_s, v, 0, UINT_MAX, e, n);
}

static int apply_rtx_pt(void *c, const char *v, char *e, size_t n) {
  unsigned u;
  if (yamlcfg_set_uint(&u, v, 0, 127, e, n)) return -1;
  ((config_t *)c)->rtx_pt = (unsigned char)u;
  return 0;
}

static int apply_workers(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->workers, v, 0, UINT_MAX, e, n);
}

static int apply_user(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->user, v, e, n);
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

static int apply_no_ret(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->no_ret, v, e, n);
}

static int apply_buffer(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->buffer_ms, v, 1, UINT_MAX, e, n);
}

static int apply_ff_port(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->ff_port, v, 0, 65535, e, n);
}

static int apply_no_mc_ret(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->no_mc_ret, v, e, n);
}

static int apply_max_ret_clients(void *c, const char *v, char *e, size_t n) {
  return set_size(&((config_t *)c)->max_ret_clients, v, 1, e, n);
}

static int apply_ret_client_idle_timeout(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->ret_client_idle_timeout_s, v, 0, UINT_MAX, e, n);
}

static int apply_no_rsi(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->no_rsi, v, e, n);
}

static int apply_rsi_interval(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->rsi_interval_s, v, 1, UINT_MAX, e, n);
}

static int apply_rsi_mc_ret(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->rsi_mc_ret, v, e, n);
}

static int apply_rsi_hostname(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (strlen(v) >= sizeof cfg->rsi_hostname) {
    snprintf(e, n, "too long (max %zu)", sizeof cfg->rsi_hostname - 1);
    return -1;
  }
  bufcpy(cfg->rsi_hostname, sizeof cfg->rsi_hostname, v);
  return 0;
}

static int apply_no_fcc(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->no_fcc, v, e, n);
}

static int apply_gop_cap(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->gop_cap_ms, v, 1, UINT_MAX, e, n);
}

static int apply_max_bursts(void *c, const char *v, char *e, size_t n) {
  return set_size(&((config_t *)c)->max_bursts, v, 1, e, n);
}

static int apply_burst_multiplier(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_double(&((config_t *)c)->burst_multiplier, v, 1.0, 1e6, 1, e, n);
}

static int apply_burst_duration_cap(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->duration_cap_ms, v, 1, UINT_MAX, e, n);
}

static int apply_max_buffer_fill_bound(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->max_buffer_fill_bound_ms, v, 0, UINT_MAX, e, n);
}

static int apply_fcc_resolve_by_port(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->fcc_resolve_by_port, v, e, n);
}

static int apply_fcc_resolve_base_port(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->fcc_resolve_base_port, v, 0, 65535, e, n);
}

static int apply_congestion_nack_threshold(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->congestion_nack_threshold, v, 0, UINT_MAX, e, n);
}

static int apply_fcc_range(void *c, const char *v, char *e, size_t n) {
  if (fccret_cfg_fcc_range(c, v)) {
    snprintf(e, n, "invalid '%s' (comma list of addr/prefix)", v);
    return -1;
  }
  return 0;
}

static int apply_fcc_client_range(void *c, const char *v, char *e, size_t n) {
  if (fccret_cfg_fcc_client_range(c, v)) {
    snprintf(e, n, "invalid '%s' (comma list of addr/prefix)", v);
    return -1;
  }
  return 0;
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

static int apply_metrics_inspect_ts(void *c, const char *v, char *e, size_t n) {
  if (metrics_inspect_ts_parse(v, &((config_t *)c)->metrics_inspect_ts)) {
    bufcpy(e, n, "must be off|basic|medium|full");
    return -1;
  }
  return 0;
}

static const yamlcfg_key_t keys[] = {
  {"range", apply_range, 0, 0},
  {"listen", apply_listen, 0, 0},
  {"iface", apply_iface, 0, 0},
  {"max-channels", apply_max_channels, 0, 0},
  {"channel-idle-timeout", apply_channel_idle_timeout, 0, 0},
  {"rtx-pt", apply_rtx_pt, 0, 0},
  {"workers", apply_workers, 0, 0},
  {"user", apply_user, 0, 0},
  {"verbose", apply_verbose, 0, 0},
  {"color", apply_color, 0, 0},
  {"daemonize", apply_daemonize, 0, 0},
  {"no-ret", apply_no_ret, 0, 0},
  {"buffer", apply_buffer, 0, 0},
  {"ff-port", apply_ff_port, 0, 0},
  {"no-mc-ret", apply_no_mc_ret, 0, 0},
  {"max-ret-clients", apply_max_ret_clients, 0, 0},
  {"ret.client-idle-timeout", apply_ret_client_idle_timeout, 0, 0},
  {"no-rsi", apply_no_rsi, 0, 0},
  {"rsi.interval", apply_rsi_interval, 0, 0},
  {"rsi.mc-ret", apply_rsi_mc_ret, 0, 0},
  {"rsi.hostname", apply_rsi_hostname, 0, 0},
  {"no-fcc", apply_no_fcc, 0, 0},
  {"gop-cap", apply_gop_cap, 0, 0},
  {"max-bursts", apply_max_bursts, 0, 0},
  {"burst-multiplier", apply_burst_multiplier, 0, 0},
  {"burst-duration-cap", apply_burst_duration_cap, 0, 0},
  {"max-buffer-fill-bound", apply_max_buffer_fill_bound, 0, 0},
  {"congestion-nack-threshold", apply_congestion_nack_threshold, 0, 0},
  {"fcc.resolve-by-port", apply_fcc_resolve_by_port, 0, 0},
  {"fcc.resolve-base-port", apply_fcc_resolve_base_port, 0, 0},
  {"fcc.range", apply_fcc_range, 0, 0},
  {"fcc.client-range", apply_fcc_client_range, 0, 0},
  {"metrics.sock", apply_metrics_sock, 0, 0},
  {"metrics.id", apply_metrics_id, 0, 0},
  {"metrics.interval", apply_metrics_interval, 0, 0},
  {"metrics.inspect-ts", apply_metrics_inspect_ts, 0, 0},
};

int fccret_cfg_load(config_t *cfg, const char *path, int strict) {
  yamlcfg_t y;
  int mode = strict ? YAMLCFG_STRICT : 0;
  int rc = yamlcfg_load(&y, TOOL_NAME, mode, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], cfg);
  return rc == YAMLCFG_ERROR ? -1 : 0;
}

static void warn_if(yamlcfg_t *y, int cond, const char *msg) {
  if (cond) yamlcfg_warn(y, "%s", msg);
}

int fccret_cfg_test(const char *path, int strict) {
  yamlcfg_t y;
  config_t cfg;

  fccret_cfg_defaults(&cfg);
  if (yamlcfg_load(&y, TOOL_NAME, strict ? YAMLCFG_CHECK | YAMLCFG_STRICT : YAMLCFG_CHECK, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], &cfg) != YAMLCFG_LOADED) return -1;

  warn_if(&y, !cfg.range_count, "range not set (required unless given on the command line)");
  warn_if(&y, !cfg.listen_port, "listen not set (required unless given on the command line)");
  warn_if(&y, !cfg.iface, "iface not set (required unless given on the command line)");
  warn_if(&y, cfg.no_ret && cfg.no_fcc, "no-ret and no-fcc together leave nothing to run");
  warn_if(&y, cfg.rsi_mc_ret && (cfg.no_mc_ret || cfg.no_ret), "rsi.mc-ret requires RET and MC RET (no-ret/no-mc-ret not set)");
  warn_if(&y, (cfg.metrics_sock || cfg.metrics_interval_s) && !cfg.metrics_id, "metrics.sock and metrics.interval require metrics.id");
  warn_if(&y, cfg.metrics_inspect_ts != METRICS_INSPECT_TS_OFF && !cfg.metrics_id, "metrics.inspect-ts requires metrics.id");
  return yamlcfg_report(&y);
}
