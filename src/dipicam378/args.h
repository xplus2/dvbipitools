/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPICAM378_ARGS_H
#define DIPICAM378_ARGS_H

typedef struct {
  const char *key_path;  /* -k, RSA private key PEM, required */
  const char *serial;    /* -s, matched against EMM-U addressing; NULL = no filter */
  unsigned port;         /* -p, cs378x TCP listen port */
  const char *username;  /* -a's "user:" part; matches reader's "user ="; NULL = no check */
  const char *password;  /* -a's password part; matches reader's "password ="; default "dipicam378" */
  unsigned caid;         /* --caid, hex; 0 = no filter */
  int cw_len;            /* --algo cissa|csa2; 16 or 8 */
  int verbose;           /* -v */
  int color_mode;        /* --color; log_color_t */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

#endif
