/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"

#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"
#include "lib/helper/strutil.h"

static const char *const shortopts = "m:p:f:P:o:t:j:Mu:x:I:c:vh";

static const struct option longopts[] = {
  {"mcast", required_argument, 0, 'm'},
  {"port", required_argument, 0, 'p'},
  {"format", required_argument, 0, 'f'},
  {"provider", required_argument, 0, 'P'},
  {"out", required_argument, 0, 'o'},
  {"timeout", required_argument, 0, 't'},
  {"jets", required_argument, 0, 'j'},
  {"mpts", no_argument, 0, 'M'},
  {"http-proxy", required_argument, 0, 'u'},
  {"http-path", required_argument, 0, 'x'},
  {"iface", required_argument, 0, 'I'},
  {"verbose", no_argument, 0, 'v'},
  {"color", required_argument, 0, OPT_COLOR},
  {"config", required_argument, 0, 'c'},
  {"config-strict", no_argument, 0, OPT_CONFIG_STRICT},
  {"configtest", no_argument, 0, OPT_CONFIGTEST},
  {"help", no_argument, 0, 'h'},
  {0, 0, 0, 0}};

static const yamlcfg_opt_t opts[] = {
    {'p', "port", "SS000165", "invalid -p port range: %s", 0},
    {'f', "format", "SS000166", "invalid -f format: %s (m3u|csv|xspf|xml|null)", 0},
    {'t', "timeout", "SS000167", "invalid -t timeout: %s (1..3600 seconds)", 0},
    {'j', "jets", "SS000168", "invalid -j jets: %s (1.." STRINGIFY(DIPISCAN_MAX_JETS) ")", 0},
    {'u', "http-proxy", "SS000169", "invalid -u http-proxy address: %s", 0},
    {'x', "http-path", "SS000170", "invalid -x path template: %s (%%g, %%p, %%%% only)", 0},
};

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  const char *cfg_path = NULL;
  int configtest = 0;
  int strict = 0;
  int c;
  int r;
  int rc;

  if (argc == 1 && !scan_cfg_default_exists()) return ARGS_NOARGS;
  if (argutil_prescan(argc, argv, shortopts, longopts, OPT_CONFIGTEST, OPT_CONFIG_STRICT, &cfg_path, &configtest, &strict)) {
    scan_print_help();
    return ARGS_HELP;
  }
  if (configtest) return scan_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;

  scan_cfg_defaults(cfg);
  if (scan_cfg_load(cfg, cfg_path, strict)) return ARGS_ERR;

  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    r = yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), scan_cfg_keys, scan_cfg_nkeys, cfg, c, optarg);
    if (r < 0) return ARGS_ERR;
    if (r > 0) continue;
    switch (c) {
      case 'm':
        rc = scan_mcast_range_parse(optarg, &cfg->family, cfg->start, cfg->end, &cfg->total);
        if (rc == RANGE_TOO_LARGE) {
          argerr("SS000701", "-m range too large: %s (max %u addresses)", optarg, MAX_SWEEP_ADDRS);
          return ARGS_ERR;
        }
        if (rc) {
          argerr("SS000164", "invalid -m address: %s (addr, addr/prefixlen, or startaddr-stopaddr)", optarg);
          return ARGS_ERR;
        }
        break;
      case 'P':
        cfg->provider = optarg;
        break;
      case 'o':
        cfg->out_path = optarg;
        break;
      case 'M':
        cfg->mpts = 1;
        break;
      case 'I':
        cfg->iface = optarg;
        break;
      case 'v':
        cfg->verbose = 1;
        break;
      case OPT_COLOR: {
        if (argutil_color_opt(TOOL_NAME, "SS000105", optarg, &cfg->color_mode)) return ARGS_ERR;
        break;
      }
      case 'c':
      case OPT_CONFIG_STRICT:
      case OPT_CONFIGTEST:
        break;
      case 'h':
        scan_print_help();
        return ARGS_HELP;
      default:
        return ARGS_ERR; /* getopt already reported */
    }
  }
  if (optind < argc) {
    argerr("SS000107", "unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  if (cfg->format == OUT_XML && !cfg->provider) {
    argerr("SS000171", "missing -P provider (required for -f xml)");
    return ARGS_ERR;
  }
  if (cfg->http_path_tmpl && !cfg->http_proxy) log_code("SS000172", TOOL_NAME ": --http-path needs -u/--http-proxy");
  return ARGS_OK;
}
