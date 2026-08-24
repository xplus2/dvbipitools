/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_ARGS_H
#define DIPIRADIOHEAD_ARGS_H

#include <stddef.h>

#include "lib/cas/biss/biss.h"
#include "lib/cas/cas_args.h"

#define RADIOHEAD_MAX_INPUTS 32
#define ARGS_MAX_RIST_PEERS 8 /* matches RISTOUT_MAX_PEERS */
#define ARGS_MAX_SRT_PEERS 8  /* matches SRTSINK_MAX_PEERS */

typedef enum { RIST_PROF_SIMPLE, RIST_PROF_MAIN } rist_profile_sel_t;
typedef enum { SRT_BOND_NONE, SRT_BOND_BROADCAST, SRT_BOND_BACKUP } srt_bond_mode_t;

typedef struct {
  const char *uri;    /* -i, icecast/shoutcast http(s) */
  unsigned sid;       /* --sid right after this -i; 0 here = auto-assign post-parse */
  char sdt_text[256]; /* --sdt right after this -i; empty here = auto-default post-parse */
} radio_input_t;

typedef struct {
  radio_input_t inputs[RADIOHEAD_MAX_INPUTS]; /* -i, repeatable; --sid/--sdt pair with -i right before them */
  unsigned n_inputs;
  int family;                /* AF_INET or AF_INET6, from -m group */
  char mcast_group[64];      /* -m group */
  unsigned mcast_port;       /* -m port */
  const char *iface;         /* -I; NULL = kernel default route */
  int rtp;                   /* -r */
  unsigned ttl;              /* -T; 0 = kernel default (1) */
  char nit_text[256];        /* -n; empty = no NIT network_name descriptor */
  long error_retry_s;        /* -e; 0 = no retry, fail on first input error (single input only) */
  int insecure_tls;          /* -k; skip TLS verification */
  unsigned tsid;             /* --tsid, default 1 */
  unsigned onid;             /* --onid, default 1 */
  int verbose;               /* -v */
  int daemonize;             /* -d, --daemonize: fork to background after startup */
  int color_mode;            /* --color; log_color_t */
  cas_algo_t cas_algo;       /* --cas-algo; NONE = CAS disabled */
  cas_vendor_t cas_vendors[ARGS_MAX_CAS_VENDORS]; /* --cas-ecmg, repeatable; per-vendor options pair with --cas-ecmg right before them */
  unsigned n_cas_vendors;
  int cas_fallback_clear; /* --cas-fallback-clear: clear instead of frozen on total outage / a required vendor down */
  unsigned cas_cp_duration_ms;      /* --cas-cp-duration; default 10000 */
  int biss2_enabled;                 /* --biss2-sw given; mutually exclusive with --cas-algo/--cas-ecmg */
  unsigned char biss2_sw[BISS_KEY_LEN]; /* --biss2-sw, parsed */
  int biss2_emit_esw;                /* --biss2-emit-esw given; requires --biss2-sw */
  unsigned char biss2_esw_id[BISS_KEY_LEN]; /* --biss2-emit-esw <id>, parsed */
  int biss1_enabled;                 /* --biss1-sw given; mutually exclusive with --biss2-sw/--cas-algo/--cas-ecmg */
  unsigned char biss1_cw[BISS1_KEY_LEN]; /* --biss1-sw, parsed into full checksummed CSA1 CW */
  int biss2_ca_enabled;              /* --biss2-ca-receivers given; mutually exclusive with --biss1-sw/--biss2-sw/--cas-algo/--cas-ecmg */
  const char *biss2_ca_receivers_dir; /* --biss2-ca-receivers <dir>: PEM public keys, one per receiver/group */
  unsigned biss2_ca_session_id;      /* --biss2-ca-session-id <hex16>; random at startup if not given */
  int biss2_ca_session_id_given;     /* --biss2-ca-session-id given */
  const char *metrics_sock;         /* --metrics; NULL = default socket path */
  const char *metrics_id;           /* --metrics-id; NULL = metrics disabled */
  unsigned metrics_interval_s;      /* --metrics-interval; 0 = default */
  char rist_uri[ARGS_MAX_RIST_PEERS][256]; /* -R/--rist, repeatable; bonded onto one sender, simultaneous with -m */
  unsigned n_rist;
  rist_profile_sel_t rist_profile; /* --profile; n_rist>0 only */
  char rist_secret[128];  /* --secret; n_rist>0 + --profile main only, "" = none */
  char rist_cname[128];   /* --cname; n_rist>0 only, "" = library default */
  unsigned rist_buffer_ms; /* --buffer; n_rist>0 only, 0 = library default */
  /* -R srt://, repeatable, bonded onto one group when >1 (--srt-group-mode).
     one scheme at a time: rist:// and srt:// peers can't mix in one -R set */
  int srt_family[ARGS_MAX_SRT_PEERS]; /* AF_INET or AF_INET6, display only */
  char srt_host[ARGS_MAX_SRT_PEERS][64];
  unsigned srt_port[ARGS_MAX_SRT_PEERS];
  unsigned n_srt;
  srt_bond_mode_t srt_group_mode; /* --srt-group-mode; n_srt>1 only, required then */
  char srt_passphrase[128];       /* --srt-passphrase; n_srt>0 only, "" = no encryption */
  int srt_pbkeylen;               /* --srt-pbkeylen, requires --srt-passphrase. 0 = library default (16) */
  char srt_streamid[128];         /* --srt-streamid; n_srt>0 only, "" = none */
  char srt_packetfilter[256];     /* --srt-packetfilter; n_srt>0 only, "" = none */
  unsigned srt_latency_ms;        /* --srt-latency; n_srt>0 only, 0 = library default */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* mcast output as text, e.g. "239.1.2.3:5000" or "[ff15::1]:5000" */
void mcast_describe(const config_t *cfg, char *buf, size_t n);

#endif
