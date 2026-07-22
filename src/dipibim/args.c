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
      "usage: %s -f xml [-i <path>] [-o <path>] [options]\n"
      "       %s -f bim [-i <path>] [-o <path>] [options]\n\n"
      "convert between the plain DVB-IPI EPG (TVA) xml shape and its BiM binary encoding\n\n"
      "options:\n"
      "  -i, --input <path>   input path, - for stdin (default)\n"
      "  -o, --output <path>  output path, - for stdout (default)\n"
      "  -f, --format <fmt>   xml|bim - names the INPUT format\n"
      "  -v, --verbose        progress on stderr\n"
      "      --color <when>   auto|always|never (default auto)\n"
      "  -h, --help           this help\n\n"
      "examples:\n"
      "  %s -f xml -i guide.tva.xml -o guide.bim\n"
      "  %s -f bim -i guide.bim -o guide.tva.xml\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"input", required_argument, 0, 'i'},
      {"output", required_argument, 0, 'o'},
      {"format", required_argument, 0, 'f'},
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1000},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_format = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  optind = 1;
  while ((c = getopt_long(argc, argv, "i:o:f:vh", longopts, NULL)) != -1) {
    switch (c) {
    case 'i':
      cfg->input_path = optarg;
      break;
    case 'o':
      cfg->output_path = optarg;
      break;
    case 'f': {
      static const enum_map_t map[] = {{"xml", FMT_XML}, {"bim", FMT_BIM}};
      int v;
      if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
        argerr("invalid -f format: %s (xml|bim)", optarg);
        return ARGS_ERR;
      }
      cfg->format = (input_fmt_t)v;
      have_format = 1;
      break;
    }
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
  if (!have_format) {
    argerr("missing -f format (xml|bim)");
    return ARGS_ERR;
  }
  return ARGS_OK;
}
