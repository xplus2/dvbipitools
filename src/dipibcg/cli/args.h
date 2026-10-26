/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBCG_ARGS_H
#define DIPIBCG_ARGS_H

#include <stddef.h>

typedef enum { MODE_ANNOUNCE, MODE_LISTEN } bcg_mode_t;

#define SCHEMA_MAX 16

typedef enum { FMT_XMLTV, FMT_TVA, FMT_BIM } bcg_fmt_t;
typedef enum { ENC_BIM, ENC_PLAIN } bcg_enc_t;

typedef struct {
  int have_a;
  int have_l;
  int have_mcast;
  int have_t;
  int have_format;
  int have_encoding;
  int have_ttl;
  int t_opt; /* 0 = -t, else OPT_INTERVAL / OPT_TIMEOUT */
  long t_value;
} args_flags_t;

typedef struct {
  args_flags_t fl;
  bcg_mode_t mode;
  const char *input_path;   /* -i, announce: xmltv source */
  const char *map_path;     /* -M, announce: xmltv id -> uri,tsid,onid,sid csv */
  long window_hours;        /* -w, announce: only include events starting within this, default 24 */
  int family;               /* AF_INET or AF_INET6, from -m group */
  char mcast_group[64];     /* -m group */
  unsigned mcast_port;      /* -m port */
  const char *iface;        /* -I; NULL = kernel default route */
  int dscp;                 /* --dscp, announce only, default NET_DSCP_SIGNALLING */
  unsigned ttl;             /* -T, announce: multicast TTL / hop limit 1..255, default 16 */
  long interval_s;          /* -t, announce: repeat interval, default 5 */
  long refresh_cycles;      /* --refresh, announce: unchanged segments sent every Nth cycle, default 1 */
  long timeout_s;           /* -t, listen: default 35 (> 30s max cycle time), 0 = until signal */
  const char *output_path;  /* -o, listen; NULL = stdout */
  const char *csvmap_path;  /* -C, listen: optional companion mapping csv */
  int compress;             /* -Z, announce: zlib-compress BCG containers (RFC 1950) */
  bcg_fmt_t format;         /* -f, announce: input, listen: output */
  bcg_enc_t encoding;       /* --encoding, announce */
  const char *schemas[SCHEMA_MAX]; /* -s: schema file or dirs */
  int nschemas;
  int dvb_locator;          /* --dvb-locator, announce: optimized locator coding (where representable) */
  int dvb_duration;         /* --dvb-duration, announce: minute coding of durations where exact */
  int dvb_pubtime;          /* --dvb-pubtime, announce: PublishedTime coding of dateTime where representable */
  const char *publisher;    /* --publisher, announce: TVAMain sent if set */
  const char *lang;         /* --lang, announce: TVAMain xml:lang */
  unsigned index_mask;      /* --index, announce: 1 << BIM_INDEX_* */
  int verbose;              /* -v */
  int daemonize;            /* -d, --daemonize: fork to background after startup */
  int color_mode;           /* --color; log_color_t */

  const char *metrics_sock;    /* --metrics; NULL = default socket path */
  const char *metrics_id;      /* --metrics-id; NULL = metrics disabled */
  unsigned metrics_interval_s; /* --metrics-interval; 0 = default */
  void *str_pool;
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* mcast as text, e.g. "239.1.2.3:5000" or "[ff15::1]:5000" */
void mcast_describe(const config_t *cfg, char *buf, size_t n);

#endif
