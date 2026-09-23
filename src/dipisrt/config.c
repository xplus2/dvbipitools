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

void srt_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->group_mode = SRTGROUP_NONE;
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

static int apply_metrics_inspect_ts(void *c, const char *v, char *e, size_t n) {
  if (metrics_inspect_ts_parse(v, &((config_t *)c)->metrics_inspect_ts)) {
    bufcpy(e, n, "must be off|basic|medium|full");
    return -1;
  }
  return 0;
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
  if (srt_cfg_add_endpoint(c, 0, v)) {
    snprintf(e, n, "invalid endpoint '%s'", v);
    return -1;
  }
  return 0;
}

static int apply_out(void *c, const char *v, char *e, size_t n) {
  if (srt_cfg_add_endpoint(c, 1, v)) {
    snprintf(e, n, "invalid endpoint '%s'", v);
    return -1;
  }
  return 0;
}

static int apply_group_mode(void *c, const char *v, char *e, size_t n) {
#ifndef DIPISRT_HAVE_BONDING
  (void)c;
  (void)v;
  snprintf(e, n, "needs a libsrt built with bonding support (ENABLE_BONDING=ON)");
  return -1;
#else
  static const enum_map_t map[] = {{"broadcast", SRTGROUP_BROADCAST}, {"backup", SRTGROUP_BACKUP}};
  int r;
  if (map_lookup(map, sizeof map / sizeof map[0], v, &r)) {
    snprintf(e, n, "invalid '%s' (broadcast|backup)", v);
    return -1;
  }
  ((config_t *)c)->group_mode = (srtgroup_mode_t)r;
  return 0;
#endif
}

static int apply_rendezvous(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->rendezvous, v, e, n);
}

static int apply_local(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_addrport(NULL, cfg->local_host, sizeof cfg->local_host, &cfg->local_port, v, e, n);
}

static int apply_passphrase(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->passphrase, sizeof cfg->passphrase, v, e, n);
}

static int apply_pbkeylen(void *c, const char *v, char *e, size_t n) {
  unsigned u;
  if (argutil_uint_range(v, 16, 32, &u) || (u != 16 && u != 24 && u != 32)) {
    snprintf(e, n, "invalid '%s' (16|24|32)", v);
    return -1;
  }
  ((config_t *)c)->pbkeylen = (int)u;
  return 0;
}

static int apply_streamid(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->streamid, sizeof cfg->streamid, v, e, n);
}

static int apply_packetfilter(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->packetfilter, sizeof cfg->packetfilter, v, e, n);
}

static int apply_latency(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->latency_ms, v, 1, 60000, e, n);
}

static int apply_send_buffer_mult(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->send_buffer_mult, v, 1, 32, e, n);
}

static const yamlcfg_key_t keys[] = {
    {"in", apply_in, 0, 1},
    {"out", apply_out, 0, 1},
    {"iface", apply_iface, 0, 0},
    {"insecure", apply_insecure, 0, 0},
    {"group-mode", apply_group_mode, 0, 0},
    {"rendezvous", apply_rendezvous, 0, 0},
    {"local", apply_local, 0, 0},
    {"passphrase", apply_passphrase, 0, 0},
    {"pbkeylen", apply_pbkeylen, 0, 0},
    {"streamid", apply_streamid, 0, 0},
    {"packetfilter", apply_packetfilter, 0, 0},
    {"latency", apply_latency, 0, 0},
    {"send-buffer-mult", apply_send_buffer_mult, 0, 0},
    {"al-fec", apply_al_fec, 0, 0},
    {"al-fec-port", apply_al_fec_port, 0, 0},
    {"metrics.sock", apply_metrics_sock, 0, 0},
    {"metrics.id", apply_metrics_id, 0, 0},
    {"metrics.interval", apply_metrics_interval, 0, 0},
    {"metrics.inspect-ts", apply_metrics_inspect_ts, 0, 0},
    {"color", apply_color, 0, 0},
    {"verbose", apply_verbose, 0, 0},
    {"daemonize", apply_daemonize, 0, 0},
};

int srt_cfg_load(config_t *cfg, const char *path, int strict) {
  yamlcfg_t y;
  int mode = strict ? YAMLCFG_STRICT : 0;
  int rc = yamlcfg_load(&y, TOOL_NAME, mode, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], cfg);
  return rc == YAMLCFG_ERROR ? -1 : 0;
}

static void warn_if(yamlcfg_t *y, int cond, const char *msg) {
  if (cond) yamlcfg_warn(y, "%s", msg);
}

int srt_cfg_test(const char *path, int strict) {
  yamlcfg_t y;
  config_t cfg;
  const endpoint_t *srt_ep;
  int both;
  srt_cfg_defaults(&cfg);
  if (yamlcfg_load(&y, TOOL_NAME, strict ? YAMLCFG_CHECK | YAMLCFG_STRICT : YAMLCFG_CHECK, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], &cfg) != YAMLCFG_LOADED) return -1;
  srt_ep = cfg.in.is_srt ? &cfg.in : &cfg.out;
  both = cfg.n_in && cfg.n_out;
  warn_if(&y, !cfg.n_in, "in not set (required unless given on the command line)");
  warn_if(&y, !cfg.n_out, "out not set (required unless given on the command line)");
  warn_if(&y, both && cfg.in.is_srt == cfg.out.is_srt, "exactly one of in/out must be srt://, the other a plain endpoint");
  warn_if(&y, both && srt_ep->n_srt > 1 && cfg.group_mode == SRTGROUP_NONE, "bonding several srt:// peers requires group-mode");
  warn_if(&y, both && srt_ep->n_srt == 1 && cfg.group_mode != SRTGROUP_NONE, "group-mode has no effect with a single srt:// peer");
  warn_if(&y, both && cfg.rendezvous && srt_ep->listen, "rendezvous is not combinable with srt://@ (listener)");
  warn_if(&y, cfg.rendezvous && cfg.group_mode != SRTGROUP_NONE, "rendezvous is not combinable with group-mode");
  warn_if(&y, cfg.rendezvous && (!cfg.local_host[0] || !cfg.local_port), "rendezvous requires local");
  warn_if(&y, cfg.passphrase[0] && (strlen(cfg.passphrase) < 10 || strlen(cfg.passphrase) > 79), "passphrase must be 10..79 characters");
  warn_if(&y, cfg.pbkeylen && !cfg.passphrase[0], "pbkeylen requires passphrase");
  warn_if(&y, (cfg.metrics_sock || cfg.metrics_interval_s) && !cfg.metrics_id, "metrics.sock and metrics.interval require metrics.id");
  warn_if(&y, cfg.metrics_inspect_ts != METRICS_INSPECT_TS_OFF && !cfg.metrics_id, "metrics.inspect-ts requires metrics.id");
  warn_if(&y, cfg.al_fec_l && !cfg.al_fec_port, "al-fec requires al-fec-port");
  warn_if(&y, !cfg.al_fec_l && cfg.al_fec_port, "al-fec-port has no effect without al-fec");
  return yamlcfg_report(&y);
}
