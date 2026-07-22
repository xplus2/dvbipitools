/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBIM_ARGS_H
#define DIPIBIM_ARGS_H

typedef enum { FMT_XML, FMT_BIM } input_fmt_t;

typedef struct {
  const char *input_path;  /* -i; NULL/"-" = stdin */
  const char *output_path; /* -o; NULL/"-" = stdout */
  input_fmt_t format;      /* -f, names the INPUT format */
  int verbose;              /* -v */
  int color_mode;           /* --color; log_color_t */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

#endif
