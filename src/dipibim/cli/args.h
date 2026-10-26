/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBIM_ARGS_H
#define DIPIBIM_ARGS_H

#include <stddef.h>

typedef enum { FMT_XML, FMT_BIM } input_fmt_t;

typedef enum { PROFILE_DVB_TVA, PROFILE_TVA, PROFILE_SDNS, PROFILE_GENERIC } profile_t;

#define PROFILE_NAMES "dvb-tva|tva|sdns|generic"

typedef enum { SYNTAX_23001, SYNTAX_15938 } syntax_t;

#define SYNTAX_NAMES "23001-1|15938-1"
#define COMPAT_NAMES "no-schema-mode|time-flags|attr-local|fixed-attr|list-string|conformance"
#define SCHEMA_MAX 16
#define ZBUF_MAX 0xFFFFFFu

typedef struct {
  const char *input_path;  /* -i; NULL/"-" = stdin */
  const char *output_path; /* -o; NULL/"-" = stdout */
  input_fmt_t format;      /* -f, names INPUT format */
  int have_format;
  int profile;             /* --profile, profile_t */
  int syntax;              /* --syntax, syntax_t, generic */
  unsigned compat;         /* --compat, BIM_COMPAT_* */
  const char *schemas[SCHEMA_MAX]; /* -s: schema file or dirs */
  int nschemas;
  unsigned len_mode;       /* --len-mode */
  int deferred;            /* --deferred */
  int cast;                /* --cast */
  int dvb_locator;         /* --dvb-locator */
  int dvb_duration;        /* --dvb-duration */
  int dvb_pubtime;         /* --dvb-pubtime */
  unsigned zbuf_size;      /* --zbuf-size, 0: default */
  int zgather;             /* --zgather */
  int verbose;             /* -v */
  int color_mode;          /* --color; log_color_t */
  void *str_pool;
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* profile name to profile_t. 0 ok, -1 unknown */
int bim_profile_parse(const char *s, int *out);
/* syntax name to syntax_t. 0 ok, -1 unknown */
int bim_syntax_parse(const char *s, int *out);
/* COMPAT_NAMES list added to *out. -1 unknown name */
int bim_compat_parse(const char *s, unsigned *out);

#endif
