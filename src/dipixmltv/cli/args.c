/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"

#include "priv.h"

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
    {"input", required_argument, 0, 'i'},
    {"output", required_argument, 0, 'o'},
    {"format", required_argument, 0, 'f'},
    {"map", required_argument, 0, 'M'},
    {"reverse-map", required_argument, 0, 'R'},
    {"suggest-map", required_argument, 0, 'S'},
    {"verbose", no_argument, 0, 'v'},
    {"color", required_argument, 0, OPT_COLOR},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}};
  int have_format = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  optind = 1;
  while ((c = getopt_long(argc, argv, "i:o:f:M:R:S:vh", longopts, NULL)) != -1) {
    switch (c) {
      case 'i':
        cfg->input_path = optarg;
        break;
      case 'o':
        cfg->output_path = optarg;
        break;
      case 'f': {
        static const enum_map_t map[] = {{"xmltv", FMT_XMLTV}, {"tva", FMT_TVA}};
        int v;
        if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
          argerr("invalid -f format: %s (xmltv|tva)", optarg);
          return ARGS_ERR;
        }
        cfg->format = (input_fmt_t)v;
        have_format = 1;
        break;
      }
      case 'M':
        cfg->map_path = optarg;
        break;
      case 'R':
        cfg->revmap_path = optarg;
        break;
      case 'S':
        cfg->suggest_scan_path = optarg;
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
      case 'h':
        xmltv_print_help();
        return ARGS_HELP;
      default:
        return ARGS_ERR;
    }
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  if (!cfg->input_path) cfg->input_path = "-";
  if (!cfg->output_path) cfg->output_path = "-";
  if (cfg->suggest_scan_path) return ARGS_OK;
  if (!have_format) {
    argerr("missing -f format (xmltv|tva)");
    return ARGS_ERR;
  }
  if (cfg->format == FMT_XMLTV && !cfg->map_path) {
    argerr("missing -M map (required for -f xmltv)");
    return ARGS_ERR;
  }
  return ARGS_OK;
}
