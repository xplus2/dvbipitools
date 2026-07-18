/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISCAN_ARGS_H
#define DIPISCAN_ARGS_H

#include <stddef.h>

typedef enum { OUT_M3U, OUT_CSV, OUT_XSPF, OUT_NULL } out_fmt_t;

typedef struct {
  int family;             /* AF_INET or AF_INET6 */
  unsigned char base[16]; /* base group address, last byte swept 1..254 */
  unsigned port_lo, port_hi; /* inclusive port range probed per address */
  out_fmt_t format;
  const char *out_path; /* NULL/"-" = stdout */
  int timeout_ms;       /* wall-clock budget per candidate address */
  int udpxy;            /* nonzero: use udpxy instead of direct join */
  char udpxy_host[256];
  unsigned udpxy_port;
  const char *iface;
  int verbose;
  int color_mode; /* log_color_t */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_NOARGS, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* base group address as text (4&6), no port */
void args_base_describe(const config_t *cfg, char *buf, size_t n);

#endif
