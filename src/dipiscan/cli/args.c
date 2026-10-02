/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "lib/helper/log.h"

#include "priv.h"

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

static args_status_t prescan(int argc, char **argv, const char **cfg_path, int *configtest, int *strict) {
  int c;
  optind = 1;
  opterr = 0;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    if (c == 'c') *cfg_path = optarg;
    if (c == OPT_CONFIGTEST) *configtest = 1;
    if (c == OPT_CONFIG_STRICT) *strict = 1;
    if (c == 'h') {
      scan_print_help();
      opterr = 1;
      return ARGS_HELP;
    }
  }
  opterr = 1;
  return ARGS_OK;
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  const char *cfg_path = NULL;
  int configtest = 0;
  int strict = 0;
  args_status_t pst;
  int c;

  if (argc == 1 && !scan_cfg_default_exists()) return ARGS_NOARGS;
  pst = prescan(argc, argv, &cfg_path, &configtest, &strict);
  if (pst != ARGS_OK) return pst;
  if (configtest) return scan_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;

  scan_cfg_defaults(cfg);
  if (scan_cfg_load(cfg, cfg_path, strict)) return ARGS_ERR;

  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    switch (c) {
      case 'm':
        if (scan_mcast_range_parse(optarg, &cfg->family, cfg->start, cfg->end, &cfg->total)) {
          argerr("invalid -m address: %s (addr, addr/prefixlen, or startaddr-stopaddr)", optarg);
          return ARGS_ERR;
        }
        break;
      case 'p':
        if (scan_port_range_parse(optarg, &cfg->port_lo, &cfg->port_hi)) {
          argerr("invalid -p port range: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 'f':
        if (scan_fmt_from_name(optarg, &cfg->format)) {
          argerr("invalid -f format: %s (m3u|csv|xspf|xml|null)", optarg);
          return ARGS_ERR;
        }
        break;
      case 'P':
        cfg->provider = optarg;
        break;
      case 'o':
        cfg->out_path = optarg;
        break;
      case 't': {
        unsigned v;
        if (argutil_uint_range(optarg, 1, 3600, &v)) {
          argerr("invalid -t timeout: %s (1..3600 seconds)", optarg);
          return ARGS_ERR;
        }
        cfg->timeout_ms = (int)(v * 1000);
        break;
      }
      case 'j': {
        unsigned v;
        if (argutil_uint_range(optarg, 1, DIPISCAN_MAX_JETS, &v)) {
          argerr("invalid -j jets: %s (1..%d)", optarg, DIPISCAN_MAX_JETS);
          return ARGS_ERR;
        }
        cfg->jets = v;
        break;
      }
      case 'M':
        cfg->mpts = 1;
        break;
      case 'u':
        if (scan_http_proxy_parse(optarg, cfg)) {
          argerr("invalid -u http-proxy address: %s", optarg);
          return ARGS_ERR;
        }
        cfg->http_proxy = 1;
        break;
      case 'x':
        if (scan_http_path_tmpl_valid(optarg)) {
          argerr("invalid -x path template: %s (%%g, %%p, %%%% only)", optarg);
          return ARGS_ERR;
        }
        cfg->http_path_tmpl = optarg;
        break;
      case 'I':
        cfg->iface = optarg;
        break;
      case 'v':
        cfg->verbose = 1;
        break;
      case OPT_COLOR: {
        log_color_t v;
        if (log_color_from_string(optarg, &v)) {
          argerr("invalid --color: %s (auto|always|never)", optarg);
          return ARGS_ERR;
        }
        cfg->color_mode = v;
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
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  if (cfg->format == OUT_XML && !cfg->provider) {
    argerr("missing -P provider (required for -f xml)");
    return ARGS_ERR;
  }
  if (cfg->http_path_tmpl && !cfg->http_proxy) log_line(TOOL_NAME ": --http-path needs -u/--http-proxy");
  return ARGS_OK;
}
