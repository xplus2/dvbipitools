/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_ARGS_H
#define DIPITVHEAD_ARGS_H

#include <stddef.h>

#include "lib/cas/biss/biss.h"
#include "lib/net/httpclient/httpclient.h"

typedef enum {
  SRC_RTP,  /* multicast, RTP wrapped */
  SRC_UDP,  /* multicast, plain ts */
  SRC_HTTP, /* http:// or https://, http_url_t.tls tells which */
  SRC_STDIN /* -i - */
} src_kind_t;

typedef struct {
  src_kind_t kind;
  /* SRC_RTP / SRC_UDP */
  int family; /* AF_INET or AF_INET6 */
  char group[64];
  unsigned port;
  /* SRC_HTTP */
  http_url_t http;
} source_t;

/* -n/-s: no flag = passthrough source table if present; "-" = drop; text = override with our own */
typedef enum { TABLE_PASSTHROUGH, TABLE_DROP, TABLE_OVERRIDE } table_mode_t;

/* NONE = --cas-algo not given, CAS disabled */
typedef enum { CAS_ALGO_NONE, CAS_ALGO_CISSA, CAS_ALGO_CSA2, CAS_ALGO_CSA1 } cas_algo_t;

typedef enum { CAS_OUTAGE_FROZEN, CAS_OUTAGE_CYCLING, CAS_OUTAGE_SILENT } cas_outage_mode_t;

#define ARGS_MAX_CAS_PIDS 16
#define ARGS_MAX_CAS_VENDORS 8 /* matches CAS_GROUP_MAX_VENDORS */
#define ARGS_MAX_INPUTS 32 /* matches MPTS_MAX_PROGRAMS: one input becomes one mux program */
#define ARGS_MAX_RIST_PEERS 8 /* matches RISTOUT_MAX_PEERS */

typedef enum { RIST_PROF_SIMPLE, RIST_PROF_MAIN } rist_profile_sel_t;

typedef struct {
  char ecmg_host[256];       /* --cas-ecmg */
  unsigned ecmg_port;        /* --cas-ecmg */
  unsigned ecmg_version;     /* --cas-ecmg-version right after this --cas-ecmg; 0 = auto-negotiate v2/v3 */
  unsigned super_cas_id;     /* --cas-super-id right after this --cas-ecmg */
  unsigned ecm_id;           /* --cas-ecm-id right after this --cas-ecmg */
  unsigned ecm_pid;          /* --cas-ecm-pid right after this --cas-ecmg; default 0x0020 */
  unsigned emmg_port;        /* --cas-emmg-port right after this --cas-ecmg; default 8002 */
  unsigned emmg_version;     /* --cas-emmg-version right after this --cas-ecmg; 0 = accept client's proposal */
  unsigned emm_pid;          /* --cas-emm-pid right after this --cas-ecmg; default 0x0021 */
  cas_outage_mode_t resilience; /* --cas-resilience right after this --cas-ecmg; default frozen */
  int required;               /* --cas-required right after this --cas-ecmg */
} cas_vendor_t;

typedef struct {
  source_t input;          /* -i */
  unsigned pmt_pid;        /* -p right after this -i; 0 = auto (first PAT program whose PMT arrives) */
  unsigned sid;            /* --sid right after this -i; 0 here = auto-assign post-parse */
  table_mode_t sdt_mode;   /* -s right after this -i */
  char sdt_text[256];      /* -s <text> right after this -i */
  const char *iface_in;    /* -I right after this -i; NULL = kernel default route */
  int strip_eit;           /* --strip-eit right after this -i */
  const char *hbbtv_url;   /* --hbbtv right after this -i; NULL = no AIT for this program */
  unsigned hbbtv_org_id;   /* --hbbtv-org-id right after this -i; required with --hbbtv */
  unsigned hbbtv_app_id;   /* --hbbtv-app-id right after this -i; required with --hbbtv */
} dipitvhead_input_t;

typedef struct {
  dipitvhead_input_t inputs[ARGS_MAX_INPUTS]; /* -i, repeatable; per-input options pair with -i right before them */
  unsigned n_inputs;
  int family;                /* AF_INET or AF_INET6, from -m group */
  char mcast_group[64];      /* -m group */
  unsigned mcast_port;       /* -m port */
  const char *iface_out;     /* -O; NULL = kernel default route */
  int rtp;                   /* default on; -u/--udp forces plain UDP output */
  unsigned ttl;              /* -T; 0 = kernel default (1) */
  table_mode_t nit_mode;     /* -n; one NIT for whole output */
  char nit_text[256];        /* -n <text> */
  unsigned bitrate_kbps;     /* -b; 0 = no shaping, passthrough rate; one shared budget for whole output */
  int stuff;                 /* -S; needs -b */
  int burst_limit;           /* -B; needs -b */
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
  unsigned cas_pids[ARGS_MAX_CAS_PIDS]; /* --cas-pids explicit numeric PIDs (output-side) */
  size_t cas_pid_count;
  int cas_pids_video; /* --cas-pids "video" token, or default when --cas-pids omitted */
  int cas_pids_audio; /* --cas-pids "audio" token, or default when --cas-pids omitted */
  unsigned cas_cp_duration_ms;     /* --cas-cp-duration; default 10000 */
  int biss2_enabled;                /* --biss2-sw given; mutually exclusive with --cas-algo/--cas-ecmg */
  unsigned char biss2_sw[BISS_KEY_LEN]; /* --biss2-sw, parsed */
  int biss2_emit_esw;               /* --biss2-emit-esw given; requires --biss2-sw */
  unsigned char biss2_esw_id[BISS_KEY_LEN]; /* --biss2-emit-esw <id>, parsed */
  int biss1_enabled;                /* --biss1-sw given; mutually exclusive with --biss2-sw/--cas-algo/--cas-ecmg */
  unsigned char biss1_cw[BISS1_KEY_LEN]; /* --biss1-sw, parsed into full checksummed CSA1 CW */
  int biss2_ca_enabled;              /* --biss2-ca-receivers given; mutually exclusive with --biss1-sw/--biss2-sw/--cas-algo/--cas-ecmg */
  const char *biss2_ca_receivers_dir; /* --biss2-ca-receivers <dir>: PEM public keys, one per receiver/group */
  unsigned biss2_ca_session_id;      /* --biss2-ca-session-id <hex16>; random at startup if not given */
  int biss2_ca_session_id_given;     /* --biss2-ca-session-id given */
  const char *metrics_sock;        /* --metrics; NULL = default socket path */
  const char *metrics_id;          /* --metrics-id; NULL = metrics disabled */
  unsigned metrics_interval_s;     /* --metrics-interval; 0 = default */
  char rist_uri[ARGS_MAX_RIST_PEERS][256]; /* -R/--rist, repeatable; bonded onto one sender, simultaneous with -m */
  unsigned n_rist;
  rist_profile_sel_t rist_profile; /* --profile; n_rist>0 only */
  char rist_secret[128];  /* --secret; n_rist>0 + --profile main only, "" = none */
  char rist_cname[128];   /* --cname; n_rist>0 only, "" = library default */
  unsigned rist_buffer_ms; /* --buffer; n_rist>0 only, 0 = library default */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* input source as text */
void source_describe(const source_t *s, char *buf, size_t n);

/* mcast output as text, e.g. "239.1.2.3:5000" or "[ff15::1]:5000" */
void mcast_describe(const config_t *cfg, char *buf, size_t n);

#endif
