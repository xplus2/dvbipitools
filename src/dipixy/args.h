/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_ARGS_H
#define DIPIXY_ARGS_H

typedef enum { LISTEN_ANY, LISTEN_V4, LISTEN_V6 } listen_scope_t;

typedef struct {
  listen_scope_t scope; /* LISTEN_ANY: "all", bind both families */
  char addr[64];        /* numeric literal, empty if scope == LISTEN_ANY */
  unsigned port;
} listen_spec_t;

typedef enum { SRC_SDS, SRC_M3U, SRC_XSPF, SRC_CSV, SRC_XML, SRC_HTTP } source_kind_t;

typedef enum { MEDIA_TV, MEDIA_RADIO } media_type_t; /* MEDIA_TV = 0, default */

typedef struct {
  source_kind_t kind;
  const char *value;        /* addr:port for SDS, path for M3U/XSPF/CSV/XML, URL for HTTP. points into argv */
  int ordinal;              /* 1-based -i position, = list index. gaps at -/rist:// slots */
  const char *name;         /* -n/--name right after this -i. NULL = unnamed, addressable by ordinal only */
  media_type_t media_type;  /* --media-type right after this -i. default tv */
} source_def_t;

typedef struct {
  const char *iface;        /* -I, NULL = kernel default */
  int insecure_tls;         /* -k, --insecure: skip TLS verification */
  listen_spec_t listen;     /* -l/--listen */
  listen_spec_t listen_tls; /* -L/--listen-tls */
  const char *tls_cert;     /* --tls-cert, NULL = search default paths */
  const char *tls_key;      /* --tls-key */
  int workers_spec;         /* -j: -1/-2/-3 relative to core count, >=1 absolute */
  int max_clients;          /* -c/--max-clients: cap on concurrent streams [256] */
  unsigned capture_ring_kib; /* --capture-ring-size: per-source ingress ring, KiB [4096] */
  double sds_timeout_s;      /* --sds-timeout: sds:// discovery wait, seconds [3] */
  double sds_refresh_interval_s; /* --sds-refresh-interval: sds:// re-poll period, seconds [30] */
  source_def_t *sources;    /* -i sources: sds://, playlist path, or http(s):// */
  int n_sources;
  int sources_cap;
  const char *stdin_path;   /* -i -. NULL = disabled */
  const char *stdin_name;   /* -n/--name right after -i -. NULL = unnamed */
  int stdin_ordinal;        /* -i position among all -i flags. 0: no stdin */
  media_type_t stdin_media_type; /* --media-type right after -i -. default tv */
  const char *rist_uri;     /* -i rist://@host:port. NULL = disabled */
  const char *rist_name;    /* -n/--name right after -i rist://... NULL = unnamed */
  int rist_ordinal;         /* -i position among all -i flags. 0: no rist */
  media_type_t rist_media_type; /* --media-type right after -i rist://... default tv */
  double segment_size;      /* --segment-size, seconds, min 2. hls/hls-fmp4/llhls/dash share it */
  int segment_count;        /* --segment-count, min 3 */
  double hls_part_size;     /* --hls-part-size, seconds, must be < segment_size */
  int hls_seg_pool;         /* --hls-seg-pool, per-size-class freelist cap, min 1 */
  const char *metrics_sock; /* --metrics. NULL = default socket path */
  const char *metrics_id;   /* --metrics-id. NULL = metrics disabled */
  unsigned metrics_interval_s; /* --metrics-interval. 0 = default */
  int metrics_http;         /* --metrics-http: serve /metrics on our own listener too */
  int no_hls;               /* -f/--format: hls absent, disables hls and hls-fmp4 routes */
  int no_llhls;             /* -f/--format: llhls absent */
  int no_dash;              /* -f/--format: dash absent */
  int no_ts;                /* -f/--format: ts absent, disables raw TS push routes */
  int no_spts;              /* -f/--format: spts absent, disables single-program TS push routes */
  int no_rawaudio;          /* -f/--format: rawaudio absent, disables /rawaudio routes */
  int no_url_rtp;           /* --no-url-rtp: disables /rtp/... routes */
  int no_url_udp;           /* --no-url-udp: disables /udp/... routes */
  int no_url_srt;           /* --no-url-srt: disables /srt/... routes */
  int no_pid_filters;       /* --no-pid-filters: ?filter= ignored */
  int no_http2;             /* --no-http2: disable h2 */
  int no_http3;             /* --no-http3: disable h3 */
  int no_fcc;               /* --no-fcc: ignore SDS fcc */
  int no_ret;               /* --no-ret: ignore SDS ret */
  int no_status;            /* --no-status: disables /ui/status.js */
  const char *status_template; /* --status-tpl <path>. NULL = embedded default. SIGHUP reloads */
  char http_auth[200];      /* --auth user:pass, precomputed "Basic <base64>". empty: disabled.
                                guards /, /ui/status.js, /ui/ws/ across h1/h2/h3 */
  const char *cors_origins;    /* --cors-origin <list>. NULL: always "*". else allowlist vs Origin hdr */
  int ssdp_ttl;              /* --ssdp-ttl: multicast TTL for SSDP packets, 1..255 */
  const char *ssdp_iface;    /* --ssdp-iface. NULL = kernel default */
  double ssdp_interval_s;   /* --ssdp-interval: NOTIFY re-announce period, seconds [60] */
  unsigned ssdp_max_age_s;  /* --ssdp-max-age: advertised CACHE-CONTROL max-age, seconds [1800] */
  int enable_dlna;           /* --enable-dlna: serve SSDP + UPnP MediaServer */
  char dlna_host[80];        /* --dlna-host, or --listen if concrete and --dlna-host unset */
  const char *dlna_name;     /* --dlna-name, NULL = default friendlyName */
  int dlna_keep_multicast;   /* --dlna-keep-multicast: rtp/udp items get dvb-igmp/dvb-mld res, skip http proxy */
  int daemonize;            /* -d, --daemonize: fork to background after startup */
  int verbose;
  int color_mode;           /* log_color_t */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_NOARGS, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* frees cfg->sources */
void args_free(config_t *cfg);

#endif
