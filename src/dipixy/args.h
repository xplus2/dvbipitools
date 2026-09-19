/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_ARGS_H
#define DIPIXY_ARGS_H

#include <stddef.h>

typedef enum { LISTEN_ANY, LISTEN_V4, LISTEN_V6 } listen_scope_t;

typedef struct {
  listen_scope_t scope; /* LISTEN_ANY: "all", bind both families */
  char addr[64];        /* numeric literal, empty if scope == LISTEN_ANY */
  unsigned port;
} listen_spec_t;

typedef enum { SRC_SDS, SRC_M3U, SRC_XSPF, SRC_CSV, SRC_XML, SRC_HTTP } source_kind_t;
const char *source_kind_str(source_kind_t k);

typedef enum { MEDIA_TV, MEDIA_RADIO } media_type_t; /* MEDIA_TV = 0, default */

typedef enum { LAST_NONE, LAST_STDIN, LAST_RIST, LAST_SOURCE } last_input_t;

typedef struct {
  source_kind_t kind;
  const char *value;        /* addr:port for SDS, path for M3U/XSPF/CSV/XML, URL for HTTP. points into argv */
  int ordinal;              /* 1-based -i position, = list index. gaps at -/rist:// slots */
  const char *name;         /* -n/--name right after this -i. NULL = unnamed, addressable by ordinal only */
  media_type_t media_type;  /* --media-type right after this -i. default tv */
} source_def_t;

typedef struct {
  const char *iface;        /* -I, NULL = kernel default */
  int insecure_tls;
  listen_spec_t listen;
  listen_spec_t listen_tls;
  const char *tls_cert;
  const char *tls_key;
  int workers_spec;         /* -j: -1/-2/-3 relative to core count, >=1 absolute */
  int max_clients;
  int max_channels;
  unsigned idle_timeout_s;
  unsigned capture_ring_kib;
  double sds_timeout_s;
  double sds_refresh_interval_s;
  int join_all;
  source_def_t *sources;    /* -i sources: sds://, playlist path, or http(s):// */
  int n_sources;
  int sources_cap;
  const char *stdin_path;
  const char *stdin_name;
  int stdin_ordinal;
  media_type_t stdin_media_type;
  const char *rist_uri;
  const char *rist_name;
  int rist_ordinal;
  media_type_t rist_media_type;
  double segment_size;
  int segment_count;
  double hls_part_size;
  double dash_part_size;
  const char *dash_utc_url;
  int hls_seg_pool;
  const char *metrics_sock;
  const char *metrics_id;
  unsigned metrics_interval_s;
  int metrics_http;
  int no_hls;
  int no_llhls;
  int no_dash;
  int no_lldash;
  int no_ts;
  int no_spts;
  int no_rawaudio;
  int no_mp4;
  int no_url_rtp;
  int no_url_udp;
  int no_url_srt;
  int no_pid_filters;
  int no_lcevc;
  int no_http2;
  int no_http3;
  unsigned h3_altsvc_port;
  int h3_retry;
  unsigned h3_max_udp;
  unsigned h3_window_kib;
  int h3_cc;
  unsigned h3_max_streams;
  unsigned h3_max_conns;
  unsigned h3_idle_s;
  int no_fcc;
  int no_ret;
  unsigned al_fec_l;
  unsigned al_fec_d;
  int no_al_fec;
  int no_status;
  const char *status_template;
  char http_auth[200];
  char http_metrics_auth[200];
  const char *cors_origins;
  int ssdp_ttl;
  const char *ssdp_iface;
  double ssdp_interval_s;
  unsigned ssdp_max_age_s;
  int enable_dlna;
  char dlna_host[80];
  const char *dlna_name;
  int dlna_keep_multicast;
  const char *dlna_host_opt; /* --dlna-host as given, resolved into dlna_host after parsing */
  int input_ordinal;         /* ordinal handed to the latest -i */
  last_input_t last_input;   /* what -n/--media-type attach to */
  int media_type_seen;       /* --media-type given for the latest -i */
  int daemonize;
  int verbose;
  int color_mode;
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_NOARGS, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* frees cfg->sources */
void args_free(config_t *cfg);

/* drops every input taken so far, next -i starts at ordinal 1 */
void dixy_cfg_reset_inputs(config_t *cfg);

/* 0 ok, -1 with reason in err */
int dixy_cfg_add_input(config_t *cfg, const char *val, char *err, size_t errsz);
int dixy_cfg_set_name(config_t *cfg, const char *name, char *err, size_t errsz);
int dixy_cfg_set_media_type(config_t *cfg, const char *val, char *err, size_t errsz);

enum { H3_RETRY_CFG_AUTO = 0, H3_RETRY_CFG_OFF, H3_RETRY_CFG_ALWAYS };
enum { H3_CC_CFG_CUBIC = 0, H3_CC_CFG_BBR, H3_CC_CFG_RENO };
int dixy_cfg_set_h3_cc(config_t *cfg, const char *val, char *err, size_t errsz);
int dixy_cfg_set_h3_retry(config_t *cfg, const char *val, char *err, size_t errsz);
int dixy_cfg_auth(const char *val, char *out, size_t outsz, char *err, size_t errsz);
int dixy_cfg_listen(listen_spec_t *out, const char *s);
int dixy_cfg_workers(int *out, const char *s);
int dixy_cfg_format(config_t *cfg, const char *s);

#endif
