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

static int mcast_parse(const char *s, config_t *cfg) {
  return uriparse_mcast_addrport(s, &cfg->family, cfg->mcast_group, sizeof cfg->mcast_group, &cfg->mcast_port);
}

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

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  const char *cfg_path = NULL;
  int configtest = 0;
  int strict = 0;
  int cli_mode = 0;
  int schema_cli = 0;
  int c;

  if (argutil_prescan(argc, argv, shortopts, longopts, OPT_CONFIGTEST, OPT_CONFIG_STRICT, &cfg_path, &configtest, &strict)) {
    bcg_print_help();
    return ARGS_HELP;
  }
  if (configtest) return bcg_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;
  bcg_cfg_defaults(cfg);
  if (bcg_cfg_load(cfg, cfg_path, strict)) return ARGS_ERR;

  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
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
      case 'w': {
        unsigned v;
        if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
          argerr("GS000102", "invalid -w window hours: %s", optarg);
          return ARGS_ERR;
        }
        cfg->window_hours = v;
        break;
      }
      case 'm':
        if (mcast_parse(optarg, cfg)) {
          argerr("GS000103", "invalid -m group:port: %s", optarg);
          return ARGS_ERR;
        }
        cfg->fl.have_mcast = 1;
        break;
      case 'I':
        cfg->iface = optarg;
        break;
      case 'T':
        if (argutil_uint_range(optarg, 1, 255, &cfg->ttl)) {
          argerr("GS000775", "invalid -T ttl: %s (1..255)", optarg);
          return ARGS_ERR;
        }
        cfg->fl.have_ttl = 1;
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
      case OPT_REFRESH: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
          argerr("GS000693", "invalid --refresh cycles: %s", optarg);
          return ARGS_ERR;
        }
        cfg->refresh_cycles = v;
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
      case OPT_PUBLISHER:
        if (!bcg_publisher_valid(optarg)) {
          argerr("GS000737", "invalid --publisher: %s (<domain>/<readable name>, RFC 3986 encoded)", optarg);
          return ARGS_ERR;
        }
        cfg->publisher = optarg;
        break;
      case OPT_LANG:
        if (!bcg_lang_valid(optarg)) {
          argerr("GS000738", "invalid --lang: %s (xs:language, e.g. en or de-AT)", optarg);
          return ARGS_ERR;
        }
        cfg->lang = optarg;
        break;
      case OPT_INDEX:
        if (bcg_index_names(optarg, &cfg->index_mask)) {
          argerr("GS000740", "invalid --index: %s (comma list of " BCG_INDEX_NAMES ")", optarg);
          return ARGS_ERR;
        }
        break;
      case 'f': {
        static const enum_map_t map[] = {{"xmltv", FMT_XMLTV}, {"tva", FMT_TVA}, {"bim", FMT_BIM}};
        int v;
        if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
          argerr("GS000763", "invalid -f format: %s (xmltv|tva|bim)", optarg);
          return ARGS_ERR;
        }
        cfg->format = (bcg_fmt_t)v;
        cfg->fl.have_format = 1;
        break;
      }
      case OPT_ENCODING: {
        static const enum_map_t map[] = {{"bim", ENC_BIM}, {"plain", ENC_PLAIN}};
        int v;
        if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
          argerr("GS000764", "invalid --encoding: %s (bim|plain)", optarg);
          return ARGS_ERR;
        }
        cfg->encoding = (bcg_enc_t)v;
        cfg->fl.have_encoding = 1;
        break;
      }
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
  cfg->mode = cfg->fl.have_l ? MODE_LISTEN : MODE_ANNOUNCE;
  if (cfg->fl.have_a == cfg->fl.have_l) {
    argerr("GS000108", "exactly one of -a/--announce or -l/--listen is required");
    return ARGS_ERR;
  }
  if (!cfg->fl.have_mcast) {
    argerr("GS000109", "missing -m multicast group:port");
    return ARGS_ERR;
  }
  if (!cfg->nschemas) {
    argerr("GS000759", "missing -s schema");
    return ARGS_ERR;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) return ARGS_ERR;

  if (cfg->mode == MODE_ANNOUNCE) {
    if (!cfg->input_path) {
      argerr("GS000110", "missing -i input");
      return ARGS_ERR;
    }
    if (!cfg->map_path && cfg->format == FMT_XMLTV) {
      argerr("GS000111", "missing -M map");
      return ARGS_ERR;
    }
    if (cfg->index_mask && (cfg->format != FMT_XMLTV || cfg->encoding != ENC_BIM)) {
      argerr("GS000765", "--index needs -f xmltv and --encoding bim");
      return ARGS_ERR;
    }
    if (cfg->lang && !cfg->publisher) {
      argerr("GS000739", "--lang requires --publisher");
      return ARGS_ERR;
    }
    if (cfg->fl.have_t) {
      if (cfg->fl.t_opt == OPT_TIMEOUT) {
        argerr("GS000690", "--timeout is listen-only (use -t/--interval)");
        return ARGS_ERR;
      }
      if (cfg->fl.t_value < 1) {
        argerr("GS000124", "invalid -t interval: must be >= 1");
        return ARGS_ERR;
      }
      cfg->interval_s = cfg->fl.t_value;
    }
  } else {
    if (cfg->metrics_id) {
      argerr("GS000112", "--metrics-id is announce-only");
      return ARGS_ERR;
    }
    if (cfg->compress) {
      argerr("GS000113", "-Z/--compress is announce-only");
      return ARGS_ERR;
    }
    if (cfg->refresh_cycles != 1) {
      argerr("GS000692", "--refresh is announce-only");
      return ARGS_ERR;
    }
    if (cfg->fl.have_encoding) {
      argerr("GS000766", "--encoding is announce-only");
      return ARGS_ERR;
    }
    if (cfg->publisher || cfg->lang || cfg->index_mask) {
      argerr("GS000741", "--publisher, --lang and --index are announce-only");
      return ARGS_ERR;
    }
    if (cfg->fl.have_ttl) {
      argerr("GS000776", "-T/--ttl is announce-only");
      return ARGS_ERR;
    }
    if (cfg->dvb_locator || cfg->dvb_duration || cfg->dvb_pubtime) {
      argerr("GS000760", "--dvb-locator, --dvb-duration and --dvb-pubtime are announce-only");
      return ARGS_ERR;
    }
    if (!cfg->output_path) cfg->output_path = "-";
    if (cfg->fl.have_t) {
      if (cfg->fl.t_opt == OPT_INTERVAL) {
        argerr("GS000691", "--interval is announce-only (use -t/--timeout)");
        return ARGS_ERR;
      }
      cfg->timeout_s = cfg->fl.t_value;
    }
  }
  return ARGS_OK;
}
