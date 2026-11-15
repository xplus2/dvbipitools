/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/uriparse.h"
#include "lib/bim/index.h"
#include "lib/tva/bcg_doc.h"
#include "lib/net/netconnect.h"

#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

void mcast_describe(const config_t *cfg, char *buf, size_t n) {
  uriparse_mcast_describe(cfg->family, cfg->mcast_group, cfg->mcast_port, buf, n);
}

static const char *const shortopts = "ali:f:M:w:m:I:T:t:o:C:c:s:Zvdh";

static const struct option longopts[] = {
  {"announce", no_argument, 0, 'a'},
  {"listen", no_argument, 0, 'l'},
  {"input", required_argument, 0, 'i'},
  {"map", required_argument, 0, 'M'},
  {"window", required_argument, 0, 'w'},
  {"mcast", required_argument, 0, 'm'},
  {"iface", required_argument, 0, 'I'},
  {"ttl", required_argument, 0, 'T'},
  {"interval", required_argument, 0, OPT_INTERVAL},
  {"timeout", required_argument, 0, OPT_TIMEOUT},
  {"refresh", required_argument, 0, OPT_REFRESH},
  {"output", required_argument, 0, 'o'},
  {"csv-map", required_argument, 0, 'C'},
  {"compress", no_argument, 0, 'Z'},
  {"publisher", required_argument, 0, OPT_PUBLISHER},
  {"lang", required_argument, 0, OPT_LANG},
  {"index", required_argument, 0, OPT_INDEX},
  {"format", required_argument, 0, 'f'},
  {"encoding", required_argument, 0, OPT_ENCODING},
  {"schema", required_argument, 0, 's'},
  {"dvb-locator", no_argument, 0, OPT_DVB_LOCATOR},
  {"dvb-duration", no_argument, 0, OPT_DVB_DURATION},
  {"dvb-pubtime", no_argument, 0, OPT_DVB_PUBTIME},
  {"verbose", no_argument, 0, 'v'},
  {"color", required_argument, 0, OPT_COLOR},
  {"metrics", required_argument, 0, OPT_METRICS},
  {"metrics-id", required_argument, 0, OPT_METRICS_ID},
  {"metrics-interval", required_argument, 0, OPT_METRICS_INTERVAL},
  {"dscp", required_argument, 0, OPT_DSCP},
  {"daemonize", no_argument, 0, 'd'},
  {"config", required_argument, 0, 'c'},
  {"config-strict", no_argument, 0, OPT_CONFIG_STRICT},
  {"configtest", no_argument, 0, OPT_CONFIGTEST},
  {"help", no_argument, 0, 'h'},
  {0, 0, 0, 0}};

static const yamlcfg_opt_t opts[] = {
    {'w', "window", "GS000102", "invalid -w window hours: %s", 0},
    {'m', "mcast", "GS000103", "invalid -m group:port: %s", 0},
    {'T', "ttl", "GS000775", "invalid -T ttl: %s (1..255)", 0},
    {OPT_REFRESH, "refresh", "GS000693", "invalid --refresh cycles: %s", 0},
    {OPT_PUBLISHER, "publisher", "GS000737", "invalid --publisher: %s (<domain>/<readable name>, RFC 3986 encoded)", 0},
    {OPT_LANG, "lang", "GS000738", "invalid --lang: %s (xs:language, e.g. en or de-AT)", 0},
    {OPT_INDEX, "index", "GS000740", "invalid --index: %s (comma list of " BCG_INDEX_NAMES ")", 0},
    {'f', "format", "GS000763", "invalid -f format: %s (xmltv|tva|bim)", 0},
    {OPT_ENCODING, "encoding", "GS000764", "invalid --encoding: %s (bim|plain)", 0},
};

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  const char *cfg_path = NULL;
  int configtest = 0;
  int strict = 0;
  int cli_mode = 0;
  int schema_cli = 0;
  int c;
  int r;

  if (argutil_prescan(argc, argv, shortopts, longopts, OPT_CONFIGTEST, OPT_CONFIG_STRICT, &cfg_path, &configtest, &strict)) {
    bcg_print_help();
    return ARGS_HELP;
  }
  if (configtest) return bcg_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;
  bcg_cfg_defaults(cfg);
  if (bcg_cfg_load(cfg, cfg_path, strict)) return ARGS_ERR;

  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    r = yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), bcg_cfg_keys, bcg_cfg_nkeys, cfg, c, optarg);
    if (r < 0) return ARGS_ERR;
    if (r > 0) continue;
    switch (c) {
      case 'a':
      case 'l':
        if (!cli_mode) {
          cfg->fl.have_a = 0;
          cfg->fl.have_l = 0;
        }
        cli_mode = 1;
        if (c == 'a') cfg->fl.have_a = 1;
        else          cfg->fl.have_l = 1;
        break;
      case OPT_CONFIG_STRICT:
      case OPT_CONFIGTEST:
      case 'c':
        break;
      case 'i':
        cfg->input_path = optarg;
        break;
      case 'M':
        cfg->map_path = optarg;
        break;
      case 'I':
        cfg->iface = optarg;
        break;
      case 't':
      case OPT_INTERVAL:
      case OPT_TIMEOUT: {
        unsigned v;
        if (argutil_uint_range(optarg, 0, UINT_MAX, &v)) {
          argerr("GS000104", "invalid -t seconds: %s", optarg);
          return ARGS_ERR;
        }
        cfg->fl.t_value = v;
        cfg->fl.have_t = 1;
        cfg->fl.t_opt = c == 't' ? 0 : c;
        break;
      }
      case 'o':
        cfg->output_path = optarg;
        break;
      case 'C':
        cfg->csvmap_path = optarg;
        break;
      case 'Z':
        cfg->compress = 1;
        break;
      case 's':
        if (!schema_cli) cfg->nschemas = 0;
        schema_cli = 1;
        if (cfg->nschemas >= SCHEMA_MAX) {
          argerr("GS000753", "too many --schema entries (max %d)", SCHEMA_MAX);
          return ARGS_ERR;
        }
        cfg->schemas[cfg->nschemas++] = optarg;
        break;
      case OPT_DVB_LOCATOR:
        cfg->dvb_locator = 1;
        break;
      case OPT_DVB_DURATION:
        cfg->dvb_duration = 1;
        break;
      case OPT_DVB_PUBTIME:
        cfg->dvb_pubtime = 1;
        break;
      case 'v':
        cfg->verbose = 1;
        break;
      case 'd':
        cfg->daemonize = 1;
        break;
      case OPT_COLOR: {
        if (argutil_color_opt(TOOL_NAME, "GS000105", optarg, &cfg->color_mode)) return ARGS_ERR;
        break;
      }
      case OPT_METRICS:
        cfg->metrics_sock = optarg;
        break;
      case OPT_METRICS_ID:
        cfg->metrics_id = optarg;
        break;
      case OPT_METRICS_INTERVAL:
        if (argutil_metrics_interval_opt(TOOL_NAME, optarg, &cfg->metrics_interval_s)) return ARGS_ERR;
        break;
      case OPT_DSCP:
        if (argutil_int_opt(TOOL_NAME, "GS000106", "dscp", optarg, net_dscp_parse, NET_DSCP_NAMES, &cfg->dscp)) return ARGS_ERR;
        break;
      case 'h':
        bcg_print_help();
        return ARGS_HELP;
      default:
        return ARGS_ERR;
    }
  }
  if (optind < argc) {
    argerr("GS000107", "unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  return bcg_cli_check(cfg);
}
