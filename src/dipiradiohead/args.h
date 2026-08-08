/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_ARGS_H
#define DIPIRADIOHEAD_ARGS_H

#include <stddef.h>

/* NONE = --cas-algo not given, CAS disabled */
typedef enum { CAS_ALGO_NONE, CAS_ALGO_CISSA, CAS_ALGO_CSA2 } cas_algo_t;

typedef enum { CAS_OUTAGE_FROZEN, CAS_OUTAGE_CYCLING, CAS_OUTAGE_SILENT } cas_outage_mode_t;

#define RADIOHEAD_MAX_INPUTS 32
#define ARGS_MAX_CAS_VENDORS 8 /* matches CAS_GROUP_MAX_VENDORS */

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
  const char *uri;    /* -i, icecast/shoutcast http(s) */
  unsigned sid;       /* --sid right after this -i; 0 here = auto-assign post-parse */
  char sdt_text[256]; /* --sdt right after this -i; empty here = auto-default post-parse */
} radio_input_t;

typedef struct {
  radio_input_t inputs[RADIOHEAD_MAX_INPUTS]; /* -i, repeatable; --sid/--sdt pair with the -i right before them */
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
  int color_mode;            /* --color; log_color_t */
  cas_algo_t cas_algo;       /* --cas-algo; NONE = CAS disabled */
  cas_vendor_t cas_vendors[ARGS_MAX_CAS_VENDORS]; /* --cas-ecmg, repeatable; per-vendor options pair with the --cas-ecmg right before them */
  unsigned n_cas_vendors;
  int cas_fallback_clear; /* --cas-fallback-clear: clear instead of frozen on total outage / a required vendor down */
  unsigned cas_cp_duration_ms;      /* --cas-cp-duration; default 10000 */
  const char *metrics_sock;         /* --metrics; NULL = default socket path */
  const char *metrics_id;           /* --metrics-id; NULL = metrics disabled */
  unsigned metrics_interval_s;      /* --metrics-interval; 0 = default */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* mcast output as text, e.g. "239.1.2.3:5000" or "[ff15::1]:5000" */
void mcast_describe(const config_t *cfg, char *buf, size_t n);

#endif
