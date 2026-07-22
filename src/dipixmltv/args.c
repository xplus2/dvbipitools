/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "args.h"
#include "version.h"

static void argerr(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void argerr(const char *fmt, ...) {
  va_list ap;
  fputs(TOOL_NAME ": ", stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}

typedef struct {
  const char *name;
  int value;
} enum_map_t;

static int map_lookup(const enum_map_t *m, size_t n, const char *s, int *out) {
  size_t i;
  for (i = 0; i < n; i++)
    if (!strcmp(s, m[i].name)) {
      *out = m[i].value;
      return 0;
    }
  return -1;
}

static void print_help(void) {
  printf(
      "usage: %s -f xmltv -M <map> [-i <path>] [-o <path>] [options]\n"
      "       %s -f tva [-R <revmap>] [-i <path>] [-o <path>] [options]\n"
      "       %s -S <scan.csv> [-i <guide.xml>] [-o <path>] [options]\n\n"
      "convert between xmltv and the DVB-IPI EPG (TVA) xml shape\n\n"
      "options:\n"
      "  -i, --input <path>     input path, - for stdin (default)\n"
      "  -o, --output <path>    output path, - for stdout (default)\n"
      "  -f, --format <fmt>     xmltv|tva - names the INPUT format\n"
      "  -M, --map <path>       xmltv id -> uri,tsid,onid,sid (required for -f xmltv)\n"
      "  -R, --reverse-map <path> uri -> preferred xmltv id (optional, -f tva only)\n"
      "  -S, --suggest-map <path> dipiscan csv - write a suggested -M mapping to -o,\n"
      "                         matched by channel name (review before use)\n"
      "  -v, --verbose          progress on stderr\n"
      "      --color <when>     auto|always|never (default auto)\n"
      "  -h, --help             this help\n\n"
      "examples:\n"
      "  %s -f xmltv -M mapping.csv -i guide.xml -o guide.tva.xml\n"
      "  %s -f tva -i guide.tva.xml -o guide.xml\n"
      "  %s -S scan.csv -i guide.xml -o mapping.csv\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"input", required_argument, 0, 'i'},
      {"output", required_argument, 0, 'o'},
      {"format", required_argument, 0, 'f'},
      {"map", required_argument, 0, 'M'},
      {"reverse-map", required_argument, 0, 'R'},
      {"suggest-map", required_argument, 0, 'S'},
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1000},
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
    case 1000: {
      static const enum_map_t map[] = {{"auto", 0}, {"always", 1}, {"never", 2}};
      int v;
      if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
        argerr("invalid --color: %s (auto|always|never)", optarg);
        return ARGS_ERR;
      }
      cfg->color_mode = v;
      break;
    }
    case 'h':
      print_help();
      return ARGS_HELP;
    default:
      return ARGS_ERR;
    }
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  if (!cfg->input_path)
    cfg->input_path = "-";
  if (!cfg->output_path)
    cfg->output_path = "-";
  if (cfg->suggest_scan_path)
    return ARGS_OK;
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
