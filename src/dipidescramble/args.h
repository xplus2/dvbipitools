/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_ARGS_H
#define DIPIDESCRAMBLE_ARGS_H

#include <stddef.h>

typedef enum { INPUT_RTP, INPUT_UDP, INPUT_STDIN } input_kind_t;
typedef enum { FMT_TS, FMT_MKV, FMT_MKA } out_fmt_t;

typedef struct {
  input_kind_t kind;
  /* INPUT_RTP / INPUT_UDP */
  int family; /* AF_INET or AF_INET6 */
  char group[64];
  unsigned port;
} input_t;

typedef struct {
  input_t input;          /* -i, required */
  const char *key_path;   /* -k, device RSA private key PEM, required */
  const char *serial;     /* -s, matched against EMM-U addressing, required */
  const char *emm_file;   /* -e, EMM cache file, required */
  const char *unicast_emm_uri; /* -u/--unicast-emm; NULL = not set. auth token as URI userinfo */
  int insecure_tls;       /* --insecure; skip TLS verification for -u/--unicast-emm */
  const char *out_path;   /* -o, descrambled output, "-" = stdout, required */
  out_fmt_t format;       /* -f; ts|mkv|mka, default ts */
  const char *iface_in;   /* -I; NULL = kernel default route */
  int verbose;            /* -v */
  int color_mode;         /* --color; log_color_t */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* input source as text */
void input_describe(const input_t *s, char *buf, size_t n);

#endif
