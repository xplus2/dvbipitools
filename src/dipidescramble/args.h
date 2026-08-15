/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_ARGS_H
#define DIPIDESCRAMBLE_ARGS_H

#include <stddef.h>

#include "ecm_profile.h"
#include "lib/cas/biss/biss.h"

typedef enum { INPUT_RTP, INPUT_UDP, INPUT_STDIN } input_kind_t;
typedef enum { FMT_TS, FMT_MKV, FMT_MKA } out_fmt_t;
typedef enum { PMT_SEL_AUTO, PMT_SEL_PID, PMT_SEL_ALL } pmt_sel_t;
typedef enum { OUT_FILE, OUT_RTMP, OUT_RTMPS } out_kind_t;

#define DIPIDESCRAMBLE_MAX_OUT 8

typedef struct {
  input_kind_t kind;
  /* INPUT_RTP / INPUT_UDP */
  int family; /* AF_INET or AF_INET6 */
  char group[64];
  unsigned port;
} input_t;

typedef struct {
  out_kind_t kind;
  char file_path[512]; /* OUT_FILE, "-" = stdout */
  char rtmp_url[600];  /* OUT_RTMP / OUT_RTMPS, passed to rtmpout's own parser as-is */
} out_target_t;

typedef struct {
  input_t input;               /* -i, required */
  const char *key_path;        /* -k, device RSA private key PEM, required unless --biss-* */
  const char *serial;          /* -s, matched against EMM-U addressing, required unless --biss-* */
  const char *emm_file;        /* -e, EMM cache file, required unless --biss-* */
  const char *unicast_emm_uri; /* NULL = not set, auth token as URI userinfo */
  int insecure_tls;            /* --insecure, skips TLS verification for -u and -o rtmps:// */
  out_target_t out[DIPIDESCRAMBLE_MAX_OUT]; /* -o, repeatable, required */
  int n_out;
  out_fmt_t format;            /* -f, ts|mkv|mka, default ts */
  pmt_sel_t pmt_sel;           /* -p, AUTO if not given */
  unsigned pmt_pid;            /* -p <pid>, valid iff pmt_sel == PMT_SEL_PID */
  const char *iface_in;        /* -I, NULL = kernel default route */
  int verbose;                 /* -v */
  int color_mode;              /* int, actually a log_color_t */
  int biss2_sw_given;                   /* --biss2-sw */
  unsigned char biss2_sw[BISS_KEY_LEN]; /* --biss2-sw, parsed */
  int biss2_esw_given;                  /* --biss2-esw, mutually exclusive with --biss2-sw */
  unsigned char biss2_esw[BISS_KEY_LEN]; /* --biss2-esw, parsed */
  unsigned char biss2_id[BISS_KEY_LEN];  /* --biss2-id, required with --biss2-esw */
  int biss1_sw_given;                    /* --biss1-sw, mutually exclusive with --biss2-sw/--biss2-esw */
  unsigned char biss1_sw[BISS1_KEY_LEN]; /* --biss1-sw, parsed into full checksummed CSA1 CW */
  const char *biss2_ca_key_path;         /* --biss2-ca-key, receiver RSA private key PEM, required only if stream turns out to be BISS Mode CA */
  ecm_profile_t ecm_profile;             /* --ecm-profile, ecm_profile.set == 0 = AES-256-ECB/CBCnoIV, unchanged */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* input source as text */
void input_describe(const input_t *s, char *buf, size_t n);

/* -o target as text */
void out_describe(const out_target_t *o, char *buf, size_t n);

#endif
