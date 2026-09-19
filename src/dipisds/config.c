/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/config/yamlcfg.h"
#include "lib/helper/argutil.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/uriparse.h"
#include "lib/net/netconnect.h"
#include "config.h"
#include "version.h"

void sds_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->format = OUT_M3U;
  cfg->dscp = NET_DSCP_SIGNALLING;
  cfg->interval_s = 5;
  cfg->timeout_s = 35;
}

static int set_byte(unsigned char *dst, const char *v, unsigned max, char *e, size_t n) {
  unsigned u;
  if (yamlcfg_set_uint(&u, v, 0, max, e, n)) return -1;
  *dst = (unsigned char)u;
  return 0;
}

static int set_long(long *dst, const char *v, unsigned min, char *e, size_t n) {
  unsigned u;
  if (yamlcfg_set_uint(&u, v, min, UINT_MAX, e, n)) return -1;
  *dst = (long)u;
  return 0;
}

static int apply_announce(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_bool(&cfg->fl.have_a, v, e, n);
}

static int apply_listen(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_bool(&cfg->fl.have_l, v, e, n);
}

static int apply_input(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->input_path, v, e, n);
}

static int apply_provider(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->provider, v, e, n);
}

static int apply_offering(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->offering, v, e, n);
}

static int apply_lang(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_lang(((config_t *)c)->lang, v, e, n);
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

static int apply_format(void *c, const char *v, char *e, size_t n) {
  static const enum_map_t map[] = {{"m3u", OUT_M3U}, {"csv", OUT_CSV}, {"xspf", OUT_XSPF}, {"xml", OUT_XML}, {"null", OUT_NULL}};
  int r;
  if (map_lookup(map, sizeof map / sizeof map[0], v, &r)) {
    snprintf(e, n, "invalid '%s' (m3u|csv|xspf|xml|null)", v);
    return -1;
  }
  ((config_t *)c)->format = (out_fmt_t)r;
  ((config_t *)c)->fl.have_format = 1;
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

static int apply_ret_addr(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_addrport(NULL, cfg->ret_addr, sizeof cfg->ret_addr, &cfg->ret_port, v, e, n)) return -1;
  cfg->ret_enabled = 1;
  return 0;
}

static int apply_ret_rtx_time(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_uint(&cfg->ret_rtx_time, v, 1, UINT_MAX, e, n)) return -1;
  cfg->fl.have_ret_rtx_time = 1;
  return 0;
}

static int apply_ret_rtx_pt(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (set_byte(&cfg->ret_rtx_pt, v, 127, e, n)) return -1;
  cfg->fl.have_ret_rtx_pt = 1;
  return 0;
}

static int apply_ret_mc(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->ret_mc, v, e, n);
}

static int apply_ret_mc_port(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_uint(&cfg->ret_mc_port, v, 1, 65535, e, n)) return -1;
  cfg->fl.have_ret_mc_port = 1;
  return 0;
}

static int apply_ret_rsi_mc_ret(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->ret_rsi_mc_ret, v, e, n);
}

static int apply_fcc_addr(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_addrport(NULL, cfg->fcc_addr, sizeof cfg->fcc_addr, &cfg->fcc_port, v, e, n)) return -1;
  cfg->fcc_enabled = 1;
  return 0;
}

static int apply_fcc_rtx_time(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_uint(&cfg->fcc_rtx_time, v, 1, UINT_MAX, e, n)) return -1;
  cfg->fl.have_fcc_rtx_time = 1;
  return 0;
}

static int apply_fcc_rtx_pt(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (set_byte(&cfg->fcc_rtx_pt, v, 127, e, n)) return -1;
  cfg->fl.have_fcc_rtx_pt = 1;
  return 0;
}

static int apply_fcc_resolve_by_port(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->fcc_resolve_by_port, v, e, n);
}

static int apply_fcc_resolve_base_port(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->fcc_resolve_base_port, v, 1, 65535, e, n);
}

static int apply_fcc_resolve_max_channels(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  unsigned u;
  if (yamlcfg_set_uint(&u, v, 1, UINT_MAX, e, n)) return -1;
  cfg->fcc_resolve_max_channels = u;
  cfg->fl.have_fcc_resolve_max_channels = 1;
  return 0;
}

static int apply_al_fec_addr(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_addrport(NULL, cfg->al_fec_addr, sizeof cfg->al_fec_addr, &cfg->al_fec_port, v, e, n)) return -1;
  cfg->al_fec_enabled = 1;
  return 0;
}

static int apply_al_fec_pt(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (set_byte(&cfg->al_fec_pt, v, 127, e, n)) return -1;
  cfg->fl.have_al_fec_pt = 1;
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

static int apply_packages(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->packages_path, v, e, n);
}

static int apply_cells(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->cells_path, v, e, n);
}

static int apply_rms_name(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_str(&cfg->rms_name, v, e, n)) return -1;
  cfg->rms_enabled = 1;
  return 0;
}

static int apply_rms_lang(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_lang(cfg->rms_lang, v, e, n)) return -1;
  cfg->fl.have_rms_lang = 1;
  return 0;
}

static int apply_rms_location(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->rms_location, v, e, n);
}

static int apply_rms_logo(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->rms_logo, v, e, n);
}

static int apply_fus_name(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_str(&cfg->fus_name, v, e, n)) return -1;
  cfg->fus_enabled = 1;
  return 0;
}

static int apply_fus_lang(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_lang(cfg->fus_lang, v, e, n)) return -1;
  cfg->fl.have_fus_lang = 1;
  return 0;
}

static int apply_fus_id(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  char *end;
  unsigned long id;
  errno = 0;
  id = strtoul(v, &end, 10);
  if (!*v || *end || errno) {
    snprintf(e, n, "invalid '%s'", v);
    return -1;
  }
  cfg->fus_id = id;
  cfg->fl.have_fus_id = 1;
  return 0;
}

static int apply_fus_announce(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_addrport(NULL, cfg->fus_announce_addr, sizeof cfg->fus_announce_addr, &cfg->fus_announce_port, v, e, n);
}

static int apply_fus_logo(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->fus_logo, v, e, n);
}

static const yamlcfg_key_t keys[] = {
    {"announce", apply_announce, 0, 0},
    {"listen", apply_listen, 0, 0},
    {"input", apply_input, 1, 0},
    {"provider", apply_provider, 0, 0},
    {"offering", apply_offering, 0, 0},
    {"lang", apply_lang, 0, 0},
    {"mcast", apply_mcast, 0, 0},
    {"iface", apply_iface, 0, 0},
    {"dscp", apply_dscp, 0, 0},
    {"interval", apply_interval, 0, 0},
    {"timeout", apply_timeout, 0, 0},
    {"output", apply_output, 0, 0},
    {"format", apply_format, 0, 0},
    {"verbose", apply_verbose, 0, 0},
    {"color", apply_color, 0, 0},
    {"daemonize", apply_daemonize, 0, 0},
    {"ret.addr", apply_ret_addr, 0, 0},
    {"ret.rtx-time", apply_ret_rtx_time, 0, 0},
    {"ret.rtx-pt", apply_ret_rtx_pt, 0, 0},
    {"ret.mc", apply_ret_mc, 0, 0},
    {"ret.mc-port", apply_ret_mc_port, 0, 0},
    {"ret.rsi-mc-ret", apply_ret_rsi_mc_ret, 0, 0},
    {"fcc.addr", apply_fcc_addr, 0, 0},
    {"fcc.rtx-time", apply_fcc_rtx_time, 0, 0},
    {"fcc.rtx-pt", apply_fcc_rtx_pt, 0, 0},
    {"fcc.resolve-by-port", apply_fcc_resolve_by_port, 0, 0},
    {"fcc.resolve-base-port", apply_fcc_resolve_base_port, 0, 0},
    {"fcc.resolve-max-channels", apply_fcc_resolve_max_channels, 0, 0},
    {"al-fec.addr", apply_al_fec_addr, 0, 0},
    {"al-fec.pt", apply_al_fec_pt, 0, 0},
    {"metrics.sock", apply_metrics_sock, 0, 0},
    {"metrics.id", apply_metrics_id, 0, 0},
    {"metrics.interval", apply_metrics_interval, 0, 0},
    {"packages", apply_packages, 1, 0},
    {"cells", apply_cells, 1, 0},
    {"rms.name", apply_rms_name, 0, 0},
    {"rms.lang", apply_rms_lang, 0, 0},
    {"rms.location", apply_rms_location, 0, 0},
    {"rms.logo", apply_rms_logo, 0, 0},
    {"fus.name", apply_fus_name, 0, 0},
    {"fus.lang", apply_fus_lang, 0, 0},
    {"fus.id", apply_fus_id, 0, 0},
    {"fus.announce", apply_fus_announce, 0, 0},
    {"fus.logo", apply_fus_logo, 0, 0},
};

int sds_cfg_load(config_t *cfg, const char *path) {
  yamlcfg_t y;
  return yamlcfg_load(&y, TOOL_NAME, 0, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof *keys, cfg) == YAMLCFG_ERROR ? -1 : 0;
}

static void warn_if(yamlcfg_t *y, int cond, const char *msg) {
  if (cond) yamlcfg_warn(y, "%s", msg);
}

static void check_conflicts(yamlcfg_t *y, const config_t *cfg) {
  const args_flags_t *fl = &cfg->fl;
  int announce_only = cfg->ret_enabled || cfg->fcc_enabled || cfg->al_fec_enabled || cfg->metrics_id || cfg->packages_path || cfg->cells_path || cfg->rms_enabled || cfg->fus_enabled || cfg->ret_mc || cfg->fcc_resolve_by_port;

  int is_xml = cfg->input_path && strlen(cfg->input_path) > 4 && !strcmp(cfg->input_path + strlen(cfg->input_path) - 4, ".xml");

  warn_if(y, !fl->have_a && !fl->have_l, "neither announce nor listen set (required unless given on the command line)");
  warn_if(y, !fl->have_mcast, "mcast not set (required unless given on the command line)");
  warn_if(y, fl->have_a && !fl->have_l && !cfg->input_path, "input not set (required for announce unless given on the command line)");
  warn_if(y, fl->have_a && !fl->have_l && cfg->input_path && !is_xml && !cfg->provider, "provider not set (required for a non-.xml input unless given on the command line)");
  warn_if(y, fl->have_a && !fl->have_l && cfg->input_path && !is_xml && !cfg->offering, "offering not set (required for a non-.xml input unless given on the command line)");
  warn_if(y, fl->have_a && fl->have_l, "announce and listen are mutually exclusive");
  warn_if(y, fl->have_l && !fl->have_a && announce_only, "ret, fcc, al-fec, metrics id, packages, cells, rms and fus are announce-only");
  warn_if(y, cfg->rms_enabled && cfg->fus_enabled, "rms and fus are mutually exclusive");
  warn_if(y, cfg->rms_enabled && !cfg->rms_location, "rms.name requires rms.location");
  warn_if(y, !cfg->rms_enabled && (fl->have_rms_lang || cfg->rms_location || cfg->rms_logo), "rms.lang, rms.location and rms.logo require rms.name");
  warn_if(y, cfg->fus_enabled && !fl->have_fus_id, "fus.name requires fus.id");
  warn_if(y, !cfg->fus_enabled && (fl->have_fus_lang || fl->have_fus_id || cfg->fus_announce_addr[0] || cfg->fus_logo), "fus.lang, fus.id, fus.announce and fus.logo require fus.name");
  warn_if(y, !cfg->ret_enabled && (fl->have_ret_rtx_time || fl->have_ret_rtx_pt || cfg->ret_mc || fl->have_ret_mc_port || cfg->ret_rsi_mc_ret),"ret.* settings require ret.addr");
  warn_if(y, cfg->ret_rsi_mc_ret && !cfg->ret_mc, "ret.rsi-mc-ret requires ret.mc");
  warn_if(y, !cfg->fcc_enabled && (fl->have_fcc_rtx_time || fl->have_fcc_rtx_pt || cfg->fcc_resolve_by_port || cfg->fcc_resolve_base_port || fl->have_fcc_resolve_max_channels), "fcc.* settings require fcc.addr");
  warn_if(y, !cfg->al_fec_enabled && fl->have_al_fec_pt, "al-fec.pt requires al-fec.addr");
  warn_if(y, (cfg->metrics_sock || cfg->metrics_interval_s) && !cfg->metrics_id, "metrics.sock and metrics.interval require metrics.id");
  warn_if(y, cfg->input_path && strlen(cfg->input_path) > 4 && !strcmp(cfg->input_path + strlen(cfg->input_path) - 4, ".xml") &&
    (cfg->ret_enabled || cfg->fcc_enabled || cfg->al_fec_enabled || cfg->packages_path || cfg->cells_path || cfg->rms_enabled || cfg->fus_enabled), "ret, fcc, al-fec, packages, cells, rms and fus have no effect with a raw .xml input");
}

int sds_cfg_test(const char *path) {
  yamlcfg_t y;
  config_t cfg;
  sds_cfg_defaults(&cfg);
  if (yamlcfg_load(&y, TOOL_NAME, 1, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof *keys, &cfg) != YAMLCFG_LOADED) return -1;
  check_conflicts(&y, &cfg);
  yamlcfg_report(&y);
  return 0;
}
