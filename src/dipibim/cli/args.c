/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <stdarg.h>
#include <string.h>

#include "lib/bim/codec.h"
#include "lib/helper/argutil.h"
#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const char *const shortopts = "i:o:f:s:c:vh";

static const struct option longopts[] = {
  {"input", required_argument, 0, 'i'},
  {"output", required_argument, 0, 'o'},
  {"format", required_argument, 0, 'f'},
  {"schema", required_argument, 0, 's'},
  {"profile", required_argument, 0, OPT_PROFILE},
  {"syntax", required_argument, 0, OPT_SYNTAX},
  {"compat", required_argument, 0, OPT_COMPAT},
  {"len-mode", required_argument, 0, OPT_LEN_MODE},
  {"deferred", no_argument, 0, OPT_DEFERRED},
  {"cast", no_argument, 0, OPT_CAST},
  {"dvb-locator", no_argument, 0, OPT_DVB_LOCATOR},
  {"dvb-duration", no_argument, 0, OPT_DVB_DURATION},
  {"dvb-pubtime", no_argument, 0, OPT_DVB_PUBTIME},
  {"zbuf-size", required_argument, 0, OPT_ZBUF_SIZE},
  {"zgather", no_argument, 0, OPT_ZGATHER},
  {"verbose", no_argument, 0, 'v'},
  {"color", required_argument, 0, OPT_COLOR},
  {"config", required_argument, 0, 'c'},
  {"config-strict", no_argument, 0, OPT_CONFIG_STRICT},
  {"configtest", no_argument, 0, OPT_CONFIGTEST},
  {"help", no_argument, 0, 'h'},
  {0, 0, 0, 0}};

int bim_profile_parse(const char *s, int *out) {
  static const enum_map_t map[] = {{"dvb-tva", PROFILE_DVB_TVA}, {"tva", PROFILE_TVA}, {"sdns", PROFILE_SDNS}, {"generic", PROFILE_GENERIC}};
  return map_lookup(map, sizeof map / sizeof map[0], s, out);
}

int bim_syntax_parse(const char *s, int *out) {
  static const enum_map_t map[] = {{"23001-1", SYNTAX_23001}, {"15938-1", SYNTAX_15938}};
  return map_lookup(map, sizeof map / sizeof map[0], s, out);
}

int bim_compat_parse(const char *s, unsigned *out) {
  static const enum_map_t map[] = {{"no-schema-mode", BIM_COMPAT_NO_SCHEMA_MODE}, {"time-flags", BIM_COMPAT_M7_FLAGS}, {"attr-local", BIM_COMPAT_ATTR_LOCAL},
                                   {"fixed-attr", BIM_COMPAT_FIXED_ATTR}, {"list-string", BIM_COMPAT_LIST_STRING},
                                   {"conformance", BIM_COMPAT_NO_SCHEMA_MODE | BIM_COMPAT_M7_FLAGS | BIM_COMPAT_ATTR_LOCAL | BIM_COMPAT_FIXED_ATTR | BIM_COMPAT_LIST_STRING}};
  while (*s) {
    size_t n = strcspn(s, ",");
    char name[32];
    int bits;
    if (n >= sizeof name) return -1;
    memcpy(name, s, n);
    name[n] = '\0';
    if (map_lookup(map, sizeof map / sizeof map[0], name, &bits) || (s[n] == ',' && !s[n + 1])) return -1;
    *out |= (unsigned)bits;
    s += n + (s[n] == ',');
  }
  return 0;
}

static const yamlcfg_opt_t opts[] = {
    {'f', "format", "BS000121", "invalid -f format: %s (xml|bim)", 0},
    {OPT_PROFILE, "profile", "BS000749", "invalid --profile: %s (" PROFILE_NAMES ")", 0},
    {OPT_SYNTAX, "syntax", "BS000769", "invalid --syntax: %s (" SYNTAX_NAMES ")", 0},
    {OPT_LEN_MODE, "len-mode", "BS000750", "invalid --len-mode: %s (0|1|2)", 0},
    {OPT_ZBUF_SIZE, "zbuf-size", "BS000751", "invalid --zbuf-size: %s (0..16777215)", 0},
};

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  const char *cfg_path = NULL;
  int configtest = 0;
  int strict = 0;
  int schema_cli = 0;
  int compat_cli = 0;
  int c;
  int r;

  if (argutil_prescan(argc, argv, shortopts, longopts, OPT_CONFIGTEST, OPT_CONFIG_STRICT, &cfg_path, &configtest, &strict)) {
    bim_print_help();
    return ARGS_HELP;
  }
  if (configtest) return bim_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;
  bim_cfg_defaults(cfg);
  if (bim_cfg_load(cfg, cfg_path, strict)) return ARGS_ERR;

  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    r = yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), bim_cfg_keys, bim_cfg_nkeys, cfg, c, optarg);
    if (r < 0) return ARGS_ERR;
    if (r > 0) continue;
    switch (c) {
      case OPT_CONFIG_STRICT:
      case OPT_CONFIGTEST:
      case 'c':
        break;
      case 'i':
        cfg->input_path = optarg;
        break;
      case 'o':
        cfg->output_path = optarg;
        break;
      case 's':
        if (!schema_cli) cfg->nschemas = 0;
        schema_cli = 1;
        if (cfg->nschemas >= SCHEMA_MAX) {
          argerr("BS000753", "too many --schema entries (max %d)", SCHEMA_MAX);
          return ARGS_ERR;
        }
        cfg->schemas[cfg->nschemas++] = optarg;
        break;
      case OPT_COMPAT:
        if (!compat_cli) cfg->compat = 0;
        compat_cli = 1;
        if (bim_compat_parse(optarg, &cfg->compat)) {
          argerr("BS000768", "invalid --compat: %s (" COMPAT_NAMES ")", optarg);
          return ARGS_ERR;
        }
        break;
      case OPT_DEFERRED:
        cfg->deferred = 1;
        break;
      case OPT_CAST:
        cfg->cast = 1;
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
      case OPT_ZGATHER:
        cfg->zgather = 1;
        break;
      case 'v':
        cfg->verbose = 1;
        break;
      case OPT_COLOR: {
        if (argutil_color_opt(TOOL_NAME, "BS000105", optarg, &cfg->color_mode)) return ARGS_ERR;
        break;
      }
      case 'h':
        bim_print_help();
        return ARGS_HELP;
      default:
        return ARGS_ERR;
    }
  }
  if (optind < argc) {
    argerr("BS000107", "unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  if (!cfg->input_path) cfg->input_path = "-";
  if (!cfg->output_path) cfg->output_path = "-";
  if (!cfg->have_format) {
    argerr("BS000122", "missing -f format (xml|bim)");
    return ARGS_ERR;
  }
  return ARGS_OK;
}
