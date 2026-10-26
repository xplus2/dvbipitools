/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "lib/bim/index.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/uriparse.h"
#include "lib/net/netconnect.h"
#include "lib/tva/bcg_doc.h"
#include "config.h"
#include "version.h"

void bcg_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->dscp = NET_DSCP_SIGNALLING;
  cfg->ttl = 16;
  cfg->window_hours = 24;
  cfg->interval_s = 5;
  cfg->refresh_cycles = 1;
  cfg->timeout_s = 35;
}

static int apply_announce(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->fl.have_a, v, e, n);
}

static int apply_listen(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->fl.have_l, v, e, n);
}

static int apply_input(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->input_path, v, e, n);
}

static int apply_map(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->map_path, v, e, n);
}

static int apply_window(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_long(&((config_t *)c)->window_hours, v, 1, e, n);
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
  return yamlcfg_set_int_by(&((config_t *)c)->dscp, v, net_dscp_parse, NET_DSCP_NAMES, e, n);
}

static int apply_ttl(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_uint(&cfg->ttl, v, 1, 255, e, n)) return -1;
  cfg->fl.have_ttl = 1;
  return 0;
}

static int apply_refresh(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_long(&((config_t *)c)->refresh_cycles, v, 1, e, n);
}

static int apply_interval(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_long(&((config_t *)c)->interval_s, v, 1, e, n);
}

static int apply_timeout(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_long(&((config_t *)c)->timeout_s, v, 0, e, n);
}

static int apply_output(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->output_path, v, e, n);
}

static int apply_csv_map(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->csvmap_path, v, e, n);
}

static int apply_compress(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->compress, v, e, n);
}

static int apply_publisher(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (!bcg_publisher_valid(v)) {
    snprintf(e, n, "invalid '%s' (<domain>/<readable name>, RFC 3986 encoded)", v);
    return -1;
  }
  return yamlcfg_set_str(&cfg->str_pool, &cfg->publisher, v, e, n);
}

static int apply_lang(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (!bcg_lang_valid(v)) {
    snprintf(e, n, "invalid '%s' (xs:language, e.g. en or de-AT)", v);
    return -1;
  }
  return yamlcfg_set_str(&cfg->str_pool, &cfg->lang, v, e, n);
}

int bcg_index_names(const char *list, unsigned *mask) {
  /* GroupInformation not produced: gi-* indices always empty */
  return bim_index_parse_names(list, mask) || (*mask & (1u << BIM_INDEX_GI_CRID | 1u << BIM_INDEX_GI_TITLE)) ? -1 : 0;
}

static int apply_index(void *c, const char *v, char *e, size_t n) {
  if (bcg_index_names(v, &((config_t *)c)->index_mask)) {
    snprintf(e, n, "invalid '%s' (comma list of " BCG_INDEX_NAMES ")", v);
    return -1;
  }
  return 0;
}

static int apply_format(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (!strcmp(v, "xmltv")) cfg->format = FMT_XMLTV;
  else if (!strcmp(v, "tva")) cfg->format = FMT_TVA;
  else if (!strcmp(v, "bim")) cfg->format = FMT_BIM;
  else {
    snprintf(e, n, "invalid '%s' (xmltv|tva|bim)", v);
    return -1;
  }
  cfg->fl.have_format = 1;
  return 0;
}

static int apply_encoding(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (!strcmp(v, "bim")) cfg->encoding = ENC_BIM;
  else if (!strcmp(v, "plain")) cfg->encoding = ENC_PLAIN;
  else {
    snprintf(e, n, "invalid '%s' (bim|plain)", v);
    return -1;
  }
  cfg->fl.have_encoding = 1;
  return 0;
}

static int apply_schema(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_add_str(&cfg->str_pool, cfg->schemas, &cfg->nschemas, SCHEMA_MAX, v, e, n);
}

static int apply_dvb_locator(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->dvb_locator, v, e, n);
}

static int apply_dvb_duration(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->dvb_duration, v, e, n);
}

static int apply_dvb_pubtime(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->dvb_pubtime, v, e, n);
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

static const yamlcfg_key_t keys[] = {
    {"announce", apply_announce, 0, 0},
    {"listen", apply_listen, 0, 0},
    {"input", apply_input, 1, 0},
    {"map", apply_map, 1, 0},
    {"window", apply_window, 0, 0},
    {"mcast", apply_mcast, 0, 0},
    {"iface", apply_iface, 0, 0},
    {"dscp", apply_dscp, 0, 0},
    {"ttl", apply_ttl, 0, 0},
    {"interval", apply_interval, 0, 0},
    {"refresh", apply_refresh, 0, 0},
    {"timeout", apply_timeout, 0, 0},
    {"output", apply_output, 0, 0},
    {"csv-map", apply_csv_map, 0, 0},
    {"compress", apply_compress, 0, 0},
    {"publisher", apply_publisher, 0, 0},
    {"lang", apply_lang, 0, 0},
    {"index", apply_index, 0, 0},
    {"format", apply_format, 0, 0},
    {"encoding", apply_encoding, 0, 0},
    {"schema", apply_schema, 1, 1},
    {"dvb-locator", apply_dvb_locator, 0, 0},
    {"dvb-duration", apply_dvb_duration, 0, 0},
    {"dvb-pubtime", apply_dvb_pubtime, 0, 0},
    {"verbose", apply_verbose, 0, 0},
    {"color", apply_color, 0, 0},
    {"daemonize", apply_daemonize, 0, 0},
    {"metrics.sock", apply_metrics_sock, 0, 0},
    {"metrics.id", apply_metrics_id, 0, 0},
    {"metrics.interval", apply_metrics_interval, 0, 0},
};

int bcg_cfg_load(config_t *cfg, const char *path, int strict) {
  yamlcfg_t y;
  int mode = strict ? YAMLCFG_STRICT : 0;
  int rc = yamlcfg_load(&y, TOOL_NAME, mode, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], cfg);
  return rc == YAMLCFG_ERROR ? -1 : 0;
}

int bcg_cfg_test(const char *path, int strict) {
  yamlcfg_t y;
  int rc;
  config_t cfg;
  const args_flags_t *fl = &cfg.fl;
  bcg_cfg_defaults(&cfg);
  if (yamlcfg_load(&y, TOOL_NAME, strict ? YAMLCFG_CHECK | YAMLCFG_STRICT : YAMLCFG_CHECK, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], &cfg) != YAMLCFG_LOADED) {
    yamlcfg_strpool_free(cfg.str_pool);
    return -1;
  }
  yamlcfg_warn_if(&y, !fl->have_a && !fl->have_l, "GA000114", "neither announce nor listen set (required unless given on the command line)");
  yamlcfg_warn_if(&y, !fl->have_mcast, "GA000115", "mcast not set (required unless given on the command line)");
  yamlcfg_warn_if(&y, fl->have_a && !fl->have_l && !cfg.input_path, "GA000116", "input not set (required for announce unless given on the command line)");
  yamlcfg_warn_if(&y, fl->have_a && !fl->have_l && cfg.format == FMT_XMLTV && !cfg.map_path, "GA000117", "map not set (required for announce unless given on the command line)");
  yamlcfg_warn_if(&y, fl->have_a && fl->have_l, "GA000118", "announce and listen are mutually exclusive");
  yamlcfg_warn_if(&y, fl->have_l && !fl->have_a && (cfg.metrics_id || cfg.compress || cfg.refresh_cycles != 1), "GA000119", "metrics.id, compress and refresh are announce-only");
  yamlcfg_warn_if(&y, !cfg.nschemas, "GA000757", "schema not set (required unless given on the command line)");
  yamlcfg_warn_if(&y, fl->have_l && !fl->have_a && (cfg.dvb_locator || cfg.dvb_duration || cfg.dvb_pubtime), "GA000758", "dvb-locator, dvb-duration and dvb-pubtime are announce-only");
  yamlcfg_warn_if(&y, cfg.lang && !cfg.publisher, "GA000739", "--lang requires --publisher");
  yamlcfg_warn_if(&y, fl->have_l && !fl->have_a && (cfg.publisher || cfg.lang || cfg.index_mask), "GA000741", "--publisher, --lang and --index are announce-only");
  yamlcfg_warn_if(&y, fl->have_l && !fl->have_a && fl->have_ttl, "GA000777", "ttl is announce-only");
  yamlcfg_warn_if(&y, (cfg.metrics_sock || cfg.metrics_interval_s) && !cfg.metrics_id, "GA000120", "metrics.sock and metrics.interval require metrics.id");
  rc = yamlcfg_report(&y);
  yamlcfg_strpool_free(cfg.str_pool);
  return rc;
}
