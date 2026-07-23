/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_ARGS_H
#define DIPITVHEAD_ARGS_H

#include <stddef.h>

#include "lib/net/httpclient.h"

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

typedef struct {
  source_t input;         /* -i */
  unsigned pmt_pid;        /* -p; 0 = auto (first PAT program whose PMT arrives) */
  int family;              /* AF_INET or AF_INET6, from -m group */
  char mcast_group[64];    /* -m group */
  unsigned mcast_port;     /* -m port */
  const char *iface;       /* -I; NULL = kernel default route */
  int rtp;                 /* -r */
  unsigned ttl;             /* -T; 0 = kernel default (1) */
  table_mode_t nit_mode;   /* -n */
  char nit_text[256];      /* -n <text> */
  table_mode_t sdt_mode;   /* -s */
  char sdt_text[256];      /* -s <text> */
  unsigned bitrate_kbps;   /* -b; 0 = no shaping, passthrough rate */
  int stuff;                /* -S; needs -b */
  int burst_limit;         /* -B; needs -b */
  int strip_eit;            /* --strip-eit */
  const char *hbbtv_url;   /* --hbbtv; NULL = no AIT sent */
  unsigned hbbtv_org_id;   /* --hbbtv-org-id; required with --hbbtv */
  unsigned hbbtv_app_id;   /* --hbbtv-app-id; required with --hbbtv */
  long error_retry_s;      /* -e; 0 = no retry, fail on first input error */
  int insecure_tls;        /* -k; skip TLS verification */
  unsigned tsid;             /* --tsid, default 1 */
  unsigned onid;             /* --onid, default 1 */
  unsigned sid;              /* --sid, default 1 */
  int verbose;               /* -v */
  int color_mode;          /* --color; log_color_t */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* input source as text */
void source_describe(const source_t *s, char *buf, size_t n);

/* mcast output as text, e.g. "239.1.2.3:5000" or "[ff15::1]:5000" */
void mcast_describe(const config_t *cfg, char *buf, size_t n);

#endif
