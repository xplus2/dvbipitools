/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_ARGS_H
#define DIPIDESCRAMBLE_ARGS_H

#include <stddef.h>

#include "ecm_profile.h"
#include "lib/cas/biss/biss.h"

typedef enum { INPUT_RTP, INPUT_UDP, INPUT_STDIN, INPUT_RIST, INPUT_SRT } input_kind_t;
typedef enum { FMT_TS, FMT_MKV, FMT_MKA } out_fmt_t;
typedef enum { PMT_SEL_AUTO, PMT_SEL_PID, PMT_SEL_ALL } pmt_sel_t;
typedef enum { OUT_FILE, OUT_RTMP, OUT_RTMPS, OUT_SRT } out_kind_t;

#define DIPIDESCRAMBLE_MAX_OUT 8

typedef struct {
  input_kind_t kind;
  /* INPUT_RTP / INPUT_UDP */
  int family; /* AF_INET or AF_INET6 */
  char group[64];
  unsigned port;
  /* INPUT_RIST */
  char rist_uri[256]; /* rist://@host:port[?query], @ required (listen) */
  /* INPUT_SRT, single peer, no bonding/rendezvous (dipisrt for that) */
  int srt_family; /* AF_INET or AF_INET6, display only */
  char srt_host[64];
  unsigned srt_port;
  int srt_listen; /* 1 = @, bind/listen/accept. 0 = call out (connect) */
} input_t;

typedef struct {
  out_kind_t kind;
  char file_path[512]; /* OUT_FILE, "-" = stdout */
  char rtmp_url[600];  /* OUT_RTMP / OUT_RTMPS, passed to rtmpout's own parser as-is */
  /* OUT_SRT: one peer per target, not bonded (repeat -o for more), always calls out */
  int srt_family; /* AF_INET or AF_INET6, display only */
  char srt_host[64];
  unsigned srt_port;
} out_target_t;

typedef struct {
  input_t input;               /* -i, required */
  const char *key_path;        /* -k, device RSA private key PEM, required unless --biss-* */
  const char *serial;          /* -s, matched against EMM-U addressing, required unless --biss-* */
  const char *emm_file;        /* -e, EMM cache file, required unless --biss-* */
  const char *unicast_emm_uri; /* NULL = not set, auth token as URI userinfo */
  int insecure_tls;            /* --insecure, skips TLS verification for -u and -o rtmps:// */
  const char *unicast_emm_token_header; /* --token-header, NULL = default "X-Device-Token" */
  out_target_t out[DIPIDESCRAMBLE_MAX_OUT]; /* -o, repeatable, required */
  int n_out;
  out_fmt_t format;            /* -f, ts|mkv|mka, default ts */
  pmt_sel_t pmt_sel;           /* -p, AUTO if not given */
  unsigned pmt_pid;            /* -p <pid>, valid iff pmt_sel == PMT_SEL_PID */
  const char *iface_in;        /* -I, NULL = kernel default route */
  int verbose;                 /* -v */
  int daemonize;               /* -d, --daemonize: fork to background after startup */
  int color_mode;              /* int, actually a log_color_t */
  int biss2_sw_given;                    /* --biss2-sw */
  unsigned char biss2_sw[BISS_KEY_LEN];  /* --biss2-sw, parsed */
  int biss2_esw_given;                   /* --biss2-esw, mutually exclusive with --biss2-sw */
  unsigned char biss2_esw[BISS_KEY_LEN]; /* --biss2-esw, parsed */
  unsigned char biss2_id[BISS_KEY_LEN];  /* --biss2-id, required with --biss2-esw */
  int biss1_sw_given;                    /* --biss1-sw, mutually exclusive with --biss2-sw/--biss2-esw */
  unsigned char biss1_sw[BISS1_KEY_LEN]; /* --biss1-sw, parsed into full checksummed CSA1 CW */
  const char *biss2_ca_key_path;         /* --biss2-ca-key, receiver RSA private key PEM, required only if stream turns out to be BISS Mode CA */
  ecm_profile_t ecm_profile;             /* --ecm-profile, ecm_profile.set == 0 = AES-256-ECB/CBCnoIV, unchanged */
  const char *metrics_sock;              /* --metrics. NULL = default socket path */
  const char *metrics_id;                /* --metrics-id. NULL = metrics disabled */
  unsigned metrics_interval_s;           /* --metrics-interval. 0 = default */
  unsigned max_services;                 /* --max-services. 0 = default (32) */
  int rist_profile_main;                 /* --profile, -i rist:// only. 0 = simple (default) */
  char srt_passphrase_in[128];           /* --srt-passphrase-in, -i srt:// only. "" = no encryption */
  int srt_pbkeylen_in;                   /* --srt-pbkeylen-in, requires --srt-passphrase-in. 0 = library default (16) */
  char srt_streamid_in[128];             /* --srt-streamid-in, -i srt:// only. "" = none */
  char srt_packetfilter_in[256];         /* --srt-packetfilter-in, -i srt:// only. "" = none */
  unsigned srt_latency_in_ms;            /* --srt-latency-in, -i srt:// only. 0 = library default */
  char srt_passphrase[128];              /* --srt-passphrase, -o srt:// only, applies to every -o srt:// target. "" = no encryption */
  int srt_pbkeylen;                      /* --srt-pbkeylen, requires --srt-passphrase. 0 = library default (16) */
  char srt_streamid[128];                /* --srt-streamid, -o srt:// only. "" = none */
  char srt_packetfilter[256];            /* --srt-packetfilter, -o srt:// only. "" = none */
  unsigned srt_latency_ms;               /* --srt-latency, -o srt:// only. 0 = library default */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* input source as text */
void input_describe(const input_t *s, char *buf, size_t n);

/* -o target as text */
void out_describe(const out_target_t *o, char *buf, size_t n);

#endif
