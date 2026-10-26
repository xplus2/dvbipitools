/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "lib/config/yamlcfg.h"
#include "config.h"
#include "version.h"

void bim_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->profile = PROFILE_DVB_TVA;
}

static int apply_input(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->input_path, v, e, n);
}

static int apply_output(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_set_str(&cfg->str_pool, &cfg->output_path, v, e, n);
}

static int apply_format(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (!strcmp(v, "xml")) cfg->format = FMT_XML;
  else if (!strcmp(v, "bim")) cfg->format = FMT_BIM;
  else {
    snprintf(e, n, "invalid '%s' (xml|bim)", v);
    return -1;
  }
  cfg->have_format = 1;
  return 0;
}

static int apply_profile(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_int_by(&((config_t *)c)->profile, v, bim_profile_parse, PROFILE_NAMES, e, n);
}

static int apply_syntax(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_int_by(&((config_t *)c)->syntax, v, bim_syntax_parse, SYNTAX_NAMES, e, n);
}

static int apply_compat(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  cfg->compat = 0;
  if (bim_compat_parse(v, &cfg->compat)) {
    snprintf(e, n, "invalid '%s' (" COMPAT_NAMES ")", v);
    return -1;
  }
  return 0;
}

static int apply_schema(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return yamlcfg_add_str(&cfg->str_pool, cfg->schemas, &cfg->nschemas, SCHEMA_MAX, v, e, n);
}

static int apply_len_mode(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->len_mode, v, 0, 2, e, n);
}

static int apply_deferred(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->deferred, v, e, n);
}

static int apply_cast(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->cast, v, e, n);
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

static int apply_zbuf_size(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->zbuf_size, v, 0, ZBUF_MAX, e, n);
}

static int apply_zgather(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->zgather, v, e, n);
}

static int apply_verbose(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->verbose, v, e, n);
}

static int apply_color(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_color(&((config_t *)c)->color_mode, v, e, n);
}

static const yamlcfg_key_t keys[] = {
    {"input", apply_input, 1, 0},
    {"output", apply_output, 0, 0},
    {"format", apply_format, 0, 0},
    {"profile", apply_profile, 0, 0},
    {"schema", apply_schema, 1, 1},
    {"syntax", apply_syntax, 0, 0},
    {"compat", apply_compat, 0, 0},
    {"len-mode", apply_len_mode, 0, 0},
    {"deferred", apply_deferred, 0, 0},
    {"cast", apply_cast, 0, 0},
    {"dvb-locator", apply_dvb_locator, 0, 0},
    {"dvb-duration", apply_dvb_duration, 0, 0},
    {"dvb-pubtime", apply_dvb_pubtime, 0, 0},
    {"zbuf-size", apply_zbuf_size, 0, 0},
    {"zgather", apply_zgather, 0, 0},
    {"verbose", apply_verbose, 0, 0},
    {"color", apply_color, 0, 0},
};

int bim_cfg_load(config_t *cfg, const char *path, int strict) {
  yamlcfg_t y;
  int rc = yamlcfg_load(&y, TOOL_NAME, strict ? YAMLCFG_STRICT : 0, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], cfg);
  return rc == YAMLCFG_ERROR ? -1 : 0;
}

int bim_cfg_test(const char *path, int strict) {
  yamlcfg_t y;
  config_t cfg;
  int rc;
  bim_cfg_defaults(&cfg);
  if (yamlcfg_load(&y, TOOL_NAME, strict ? YAMLCFG_CHECK | YAMLCFG_STRICT : YAMLCFG_CHECK, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], &cfg) != YAMLCFG_LOADED) {
    yamlcfg_strpool_free(cfg.str_pool);
    return -1;
  }
  rc = yamlcfg_report(&y);
  yamlcfg_strpool_free(cfg.str_pool);
  return rc;
}
