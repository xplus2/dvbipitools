/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIMETRICS_ARGS_H
#define DIPIMETRICS_ARGS_H

typedef struct {
  const char *sock_path; /* -S, --sock; UDS to receive snapshots on */
  int family;            /* AF_INET or AF_INET6, from -l addr */
  char listen_addr[64];  /* -l addr part */
  unsigned listen_port;  /* -l port part */
  long expiry_s;         /* -e, --expiry: drop an instance after this long silent */
  int verbose;           /* -v */
  int color_mode;        /* --color; log_color_t */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

#endif
