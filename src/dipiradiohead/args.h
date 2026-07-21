/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_ARGS_H
#define DIPIRADIOHEAD_ARGS_H

#include <stddef.h>

typedef struct {
  const char *input_uri; /* -i, icecast/shoutcast http(s) */
  int family;            /* AF_INET or AF_INET6, from -m group */
  char mcast_group[64];  /* -m group */
  unsigned mcast_port;   /* -m port */
  const char *iface;     /* -I; NULL = kernel default route */
  int rtp;               /* -r */
  char nit_text[256];    /* -n; empty = no NIT network_name descriptor */
  char sdt_text[256];    /* -s; empty = "dipiradiohead" */
  long error_retry_s;    /* -e; 0 = no retry, fail on first input error */
  int insecure_tls;      /* -k; skip TLS verification */
  unsigned tsid;         /* --tsid, default 1 */
  unsigned onid;         /* --onid, default 1 */
  unsigned sid;          /* --sid, default 1 */
  int verbose;           /* -v */
  int color_mode;        /* --color; log_color_t */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* mcast output as text, e.g. "239.1.2.3:5000" or "[ff15::1]:5000" */
void mcast_describe(const config_t *cfg, char *buf, size_t n);

#endif
