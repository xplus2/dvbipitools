/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISDS_ARGS_H
#define DIPISDS_ARGS_H

#include <stddef.h>

typedef enum { MODE_ANNOUNCE, MODE_LISTEN } sds_mode_t;
typedef enum { OUT_M3U, OUT_CSV, OUT_XSPF, OUT_XML, OUT_NULL } out_fmt_t;

typedef struct {
  sds_mode_t mode;
  const char *input_path;  /* -i, announce */
  const char *provider;    /* -p, announce: DomainName */
  const char *offering;    /* -O, announce: display Name */
  char lang[4];            /* -L, announce: ISO 639-2 for the Name; default "deu" */
  int family;              /* AF_INET or AF_INET6, from -m group */
  char mcast_group[64];    /* -m group */
  unsigned mcast_port;     /* -m port */
  const char *iface;       /* -I; NULL = kernel default route */
  long interval_s;         /* -t, announce: repeat interval, default 5 */
  long timeout_s;          /* -t, listen: default 35 (> 30s max cycle time) */
  const char *output_path; /* -o, listen; NULL = stdout */
  out_fmt_t format;        /* -f, listen */
  int verbose;             /* -v */
  int color_mode;          /* --color; log_color_t */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* mcast as text, e.g. "239.1.2.3:5000" or "[ff15::1]:5000" */
void mcast_describe(const config_t *cfg, char *buf, size_t n);

#endif
