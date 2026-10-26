/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/sdns.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/playlist_out.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/uriparse.h"
#include "lib/net/netconnect.h"
#include "config.h"
#include "version.h"

void sds_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->format = OUT_M3U;
  cfg->dscp = NET_DSCP_SIGNALLING;
  cfg->ttl = 16;
  cfg->interval_s = 5;
  cfg->timeout_s = 35;
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
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->input_path, v, e, n);
}

static int apply_provider(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->provider, v, e, n);
}

static int apply_offering(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->offering, v, e, n);
}

static int apply_lang(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_lang(((config_t *)c)->lang, v, e, n);
}

static int apply_mcast(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (uriparse_mcast_cfg(v, &cfg->family, cfg->mcast_group, sizeof cfg->mcast_group, &cfg->mcast_port, e, n)) return -1;
  cfg->fl.have_mcast = 1;
  return 0;
}

static int apply_iface(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->iface, v, e, n);
}

static int apply_dscp(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_int_by(&cfg->dscp, v, net_dscp_parse, NET_DSCP_NAMES, e, n)) return -1;
  cfg->fl.have_dscp = 1;
  return 0;
}

static int apply_ttl(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_uint(&cfg->ttl, v, 1, 255, e, n)) return -1;
  cfg->fl.have_ttl = 1;
  return 0;
}

static int apply_interval(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_long(&((config_t *)c)->interval_s, v, 1, e, n);
}

static int apply_timeout(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_long(&((config_t *)c)->timeout_s, v, 0, e, n);
}

static int apply_sdns_ns(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->sdns_ns, v, e, n);
}

static int apply_schema(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_add_str(&cfg->str_pool, cfg->schemas, &cfg->nschemas, SCHEMA_MAX, v, e, n);
}

static int apply_output(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->output_path, v, e, n);
}

static int apply_format(void *c, const char *v, char *e, size_t n) {
  int r;
  if (playlist_out_fmt_parse(v, &r)) {
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
  if (yamlcfg_set_u8(&cfg->ret_rtx_pt, v, 127, e, n)) return -1;
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
  if (yamlcfg_set_u8(&cfg->fcc_rtx_pt, v, 127, e, n)) return -1;
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
  if (yamlcfg_set_u8(&cfg->al_fec_pt, v, 127, e, n)) return -1;
  cfg->fl.have_al_fec_pt = 1;
  return 0;
}

static int apply_metrics_sock(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->metrics_sock, v, e, n);
}

static int apply_metrics_id(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->metrics_id, v, e, n);
}

static int apply_metrics_interval(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->metrics_interval_s, v, 1, 86400, e, n);
}

static int apply_packages(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->packages_path, v, e, n);
}

static int apply_cells(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->cells_path, v, e, n);
}

static int apply_rms_name(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_str(&cfg->str_pool, &cfg->rms_name, v, e, n)) return -1;
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
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->rms_location, v, e, n);
}

static int apply_rms_logo(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->rms_logo, v, e, n);
}

static int apply_fus_name(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_str(&cfg->str_pool, &cfg->fus_name, v, e, n)) return -1;
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
  unsigned long id;
  if (parse_ulong(v, ULONG_MAX, &id)) {
    snprintf(e, n, "invalid '%s'", v);
    return -1;
  }
  cfg->fus_id = id;
  cfg->fl.have_fus_id = 1;
  return 0;
}

static int apply_fus_announce(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  int family;
  return uriparse_mcast_cfg(v, &family, cfg->fus_announce_addr, sizeof cfg->fus_announce_addr, &cfg->fus_announce_port, e, n);
}

static int apply_bcg_name(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  cfg->bcg_enabled = 1;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->bcg_name, v, e, n);
}

static int apply_bcg_id(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->bcg_id, v, e, n);
}

static int apply_bcg_description(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->bcg_description, v, e, n);
}

static int apply_bcg_provider_name(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->bcg_provider_name, v, e, n);
}

static int apply_bcg_mcast(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  int family;
  return uriparse_mcast_cfg(v, &family, cfg->bcg_mcast_addr, sizeof cfg->bcg_mcast_addr, &cfg->bcg_mcast_port, e, n);
}

static int apply_fus_logo(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->fus_logo, v, e, n);
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
    {"ttl", apply_ttl, 0, 0},
    {"interval", apply_interval, 0, 0},
    {"timeout", apply_timeout, 0, 0},
    {"bim-namespace", apply_sdns_ns, 0, 0},
    {"schema", apply_schema, 1, 1},
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
    {"bcg.name", apply_bcg_name, 0, 0},
    {"bcg.id", apply_bcg_id, 0, 0},
    {"bcg.description", apply_bcg_description, 0, 0},
    {"bcg.provider-name", apply_bcg_provider_name, 0, 0},
    {"bcg.mcast", apply_bcg_mcast, 0, 0},
};

int sds_cfg_load(config_t *cfg, const char *path, int strict) {
  yamlcfg_t y;
  int mode = strict ? YAMLCFG_STRICT : 0;
  int rc = yamlcfg_load(&y, TOOL_NAME, mode, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], cfg);
  return rc == YAMLCFG_ERROR ? -1 : 0;
}

static void check_conflicts(yamlcfg_t *y, const config_t *cfg) {
  const args_flags_t *fl = &cfg->fl;
  int announce_only = sds_announce_only_set(cfg);
  int announce = fl->have_a && !fl->have_l;
  int listen = fl->have_l && !fl->have_a;
  int is_xml = cfg->input_path && sds_has_suffix(cfg->input_path, ".xml");

  yamlcfg_warn_if(y, !fl->have_a && !fl->have_l, "AA000114", "neither announce nor listen set (required unless given on the command line)");
  yamlcfg_warn_if(y, !fl->have_mcast, "AA000115", "mcast not set (required unless given on the command line)");
  yamlcfg_warn_if(y, fl->have_a && !fl->have_l && !cfg->input_path, "AA000116", "input not set (required for announce unless given on the command line)");
  yamlcfg_warn_if(y, fl->have_a && !fl->have_l && cfg->input_path && !is_xml && !cfg->provider, "AA000211", "provider not set (required for a non-.xml input unless given on the command line)");
  yamlcfg_warn_if(y, fl->have_a && !fl->have_l && cfg->input_path && !is_xml && !cfg->offering, "AA000212", "offering not set (required for a non-.xml input unless given on the command line)");
  yamlcfg_warn_if(y, fl->have_a && fl->have_l, "AA000118", "announce and listen are mutually exclusive");
  yamlcfg_warn_if(y, listen && announce_only, "AA000213", "ret, fcc, al-fec, metrics id, packages, cells, rms, fus and bcg are announce-only");
  yamlcfg_warn_if(y, listen && sds_announce_basics_set(cfg), "AA000773", "input, provider, offering, lang, dscp and ttl are announce-only");
  yamlcfg_warn_if(y, listen && cfg->sdns_ns && !cfg->nschemas && !sdns_set_find(cfg->sdns_ns), "AA000742", "unsupported BiM SD&S namespace");
  yamlcfg_warn_if(y, announce && cfg->nschemas, "AA000762", "schema is listen-only");
  yamlcfg_warn_if(y, announce && sds_listen_only_set(cfg), "AA000772", "output, format and bim-namespace are listen-only");
  yamlcfg_warn_if(y, announce && cfg->fcc_enabled && cfg->fcc_resolve_by_port && sds_fcc_resolve_last_port(cfg) > 65535UL, "AA000698", "fcc.resolve-by-port port range exceeds 65535");
  yamlcfg_warn_if(y, cfg->rms_enabled && cfg->fus_enabled, "AA000214", "rms and fus are mutually exclusive");
  yamlcfg_warn_if(y, cfg->rms_enabled && !cfg->rms_location, "AA000215", "rms.name requires rms.location");
  yamlcfg_warn_if(y, !cfg->rms_enabled && (fl->have_rms_lang || cfg->rms_location || cfg->rms_logo), "AA000216", "rms.lang, rms.location and rms.logo require rms.name");
  yamlcfg_warn_if(y, cfg->fus_enabled && !fl->have_fus_id, "AA000217", "fus.name requires fus.id");
  yamlcfg_warn_if(y, !cfg->fus_enabled && (fl->have_fus_lang || fl->have_fus_id || cfg->fus_announce_addr[0] || cfg->fus_logo), "AA000218", "fus.lang, fus.id, fus.announce and fus.logo require fus.name");
  yamlcfg_warn_if(y, !cfg->bcg_enabled && sds_bcg_opts_set(cfg), "AA000732", "--bcg-id/--bcg-description/--bcg-provider-name/--bcg-mcast require --bcg-name");
  yamlcfg_warn_if(y, cfg->bcg_enabled && !cfg->bcg_mcast_addr[0], "AA000733", "--bcg-name requires --bcg-mcast");
  yamlcfg_warn_if(y, !cfg->ret_enabled && (fl->have_ret_rtx_time || fl->have_ret_rtx_pt || cfg->ret_mc || fl->have_ret_mc_port || cfg->ret_rsi_mc_ret),"AA000219", "ret.* settings require ret.addr");
  yamlcfg_warn_if(y, cfg->ret_rsi_mc_ret && !cfg->ret_mc, "AA000220", "ret.rsi-mc-ret requires ret.mc");
  yamlcfg_warn_if(y, !cfg->fcc_enabled && (fl->have_fcc_rtx_time || fl->have_fcc_rtx_pt || cfg->fcc_resolve_by_port || cfg->fcc_resolve_base_port || fl->have_fcc_resolve_max_channels), "AA000221", "fcc.* settings require fcc.addr");
  yamlcfg_warn_if(y, !cfg->al_fec_enabled && fl->have_al_fec_pt, "AA000222", "al-fec.pt requires al-fec.addr");
  yamlcfg_warn_if(y, (cfg->metrics_sock || cfg->metrics_interval_s) && !cfg->metrics_id, "AA000120", "metrics.sock and metrics.interval require metrics.id");
  yamlcfg_warn_if(y, is_xml &&
    (cfg->ret_enabled || cfg->fcc_enabled || cfg->al_fec_enabled || cfg->packages_path || cfg->cells_path || cfg->rms_enabled || cfg->fus_enabled || sds_bcg_set(cfg)), "AA000223", "ret, fcc, al-fec, packages, cells, rms, fus and bcg have no effect with a raw .xml input");
}

int sds_cfg_test(const char *path, int strict) {
  yamlcfg_t y;
  int rc;
  config_t cfg;
  sds_cfg_defaults(&cfg);
  if (yamlcfg_load(&y, TOOL_NAME, strict ? YAMLCFG_CHECK | YAMLCFG_STRICT : YAMLCFG_CHECK, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], &cfg) != YAMLCFG_LOADED) {
    yamlcfg_strpool_free(cfg.str_pool);
    return -1;
  }
  check_conflicts(&y, &cfg);
  rc = yamlcfg_report(&y);
  yamlcfg_strpool_free(cfg.str_pool);
  return rc;
}
