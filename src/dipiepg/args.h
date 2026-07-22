/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIEPG_ARGS_H
#define DIPIEPG_ARGS_H

#include <stddef.h>

typedef enum { MODE_ANNOUNCE, MODE_LISTEN } epg_mode_t;

typedef struct {
  epg_mode_t mode;
  const char *input_path;  /* -i, announce: xmltv source */
  const char *map_path;    /* -M, announce: xmltv id -> uri,tsid,onid,sid csv */
  long window_hours;       /* -w, announce: only include events starting within this, default 24 */
  int family;               /* AF_INET or AF_INET6, from -m group */
  char mcast_group[64];     /* -m group */
  unsigned mcast_port;      /* -m port */
  const char *iface;        /* -I; NULL = kernel default route */
  long interval_s;          /* -t, announce: repeat interval, default 5 */
  long timeout_s;           /* -t, listen: default 35 (> 30s max cycle time) */
  const char *output_path;  /* -o, listen; NULL = stdout */
  const char *csvmap_path;  /* -C, listen: optional companion mapping csv */
  int verbose;              /* -v */
  int color_mode;           /* --color; log_color_t */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* mcast as text, e.g. "239.1.2.3:5000" or "[ff15::1]:5000" */
void mcast_describe(const config_t *cfg, char *buf, size_t n);

#endif
