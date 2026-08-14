/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_ARGS_H
#define DIPIREC_ARGS_H

#include <stddef.h>

typedef enum {
  URI_RTP,  /* multicast, RTP wrapped */
  URI_UDP,  /* multicast, plain ts */
  URI_UDPXY, /* udpxy http */
  URI_FILE  /* stdin ("-") or a local file path; RTP-vs-raw auto-detected */
} uri_kind_t;

typedef struct {
  uri_kind_t kind;
  int rtp_wrapped; /* RTP payload */
  /* URI_RTP / URI_UDP */
  int family; /* AF_INET or AF_INET6 */
  char group[64];
  unsigned port;
  /* URI_UDPXY */
  char http_host[256];
  unsigned http_port;
  char http_path[512]; /* GET path, leading '/' */
  /* URI_FILE; "" means stdin */
  char file_path[512];
} source_t;

typedef enum { FMT_RAW, FMT_TS, FMT_MKV, FMT_MKA } out_fmt_t;
typedef enum { SUB_KEEP, SUB_STRIP, SUB_SRT } sub_mode_t;
typedef enum { PMT_SEL_AUTO, PMT_SEL_PID, PMT_SEL_ALL } pmt_sel_t;
typedef enum { OUT_FILE, OUT_RTP, OUT_UDP, OUT_RIST } out_kind_t;
typedef enum { RIST_PROF_SIMPLE, RIST_PROF_MAIN } rist_profile_sel_t;

typedef struct {
  out_kind_t kind;
  /* OUT_RTP / OUT_UDP */
  int family; /* AF_INET or AF_INET6 */
  char group[64];
  unsigned port;
  /* OUT_FILE; "-" = stdout */
  char file_path[512];
  /* OUT_RIST; passed to librist's own rist://... parser as-is */
  char rist_uri[256];
} out_target_t;

typedef struct {
  int enabled;     /* --ret given; RET client off otherwise, no behavior change */
  int family;      /* AF_INET or AF_INET6 */
  char addr[64];   /* RET server unicast address */
  unsigned port;
  int mc_enabled;  /* join MC repair session; default on, --no-ret-mc clears */
  unsigned mc_port; /* --ret-mc-port; 0 = reuse -i's port, per F.6.2.2 */
  unsigned char rtx_pt; /* --ret-pt; must match RET server's -R */
  unsigned wait_ms; /* --ret-wait; hold budget after a NACK before giving up on a gap */
} ret_cfg_t;

typedef struct {
  out_target_t out;     /* -o */
  source_t source;      /* -i */
  int audio_all;        /* -a all */
  unsigned audio_track; /* -a N, 1-based, !audio_all */
  out_fmt_t format;     /* -f resolved */
  pmt_sel_t pmt_sel;    /* -p; AUTO if not given */
  unsigned pmt_pid;     /* -p <pid>; valid iff pmt_sel == PMT_SEL_PID */
  sub_mode_t subs;      /* -s */
  long duration_s;      /* -t seconds; 0 = until stopped */
  const char *iface_in;  /* -I; -i rtp/udp join iface, NULL = kernel default */
  const char *iface_out; /* --out-iface; -o rtp/udp send iface, NULL = kernel default */
  int out_ttl;           /* --ttl; -o rtp/udp send TTL/hop-limit, 0 = kernel default (1) */
  int verbose;          /* -v */
  long sub_lead_ms;     /* --sub-lead; subtitles shifted earlier */
  int color_mode;       /* --color; log_color_t */
  ret_cfg_t ret;        /* --ret and friends; RTP source only */
  int pace;             /* --pace; URI_FILE source only */
  unsigned strip_mask;  /* --strip; STRIP_* bits from filter/ts.h, -f ts only */
  rist_profile_sel_t rist_profile; /* --profile; -o rist:// only */
  char rist_secret[128];  /* --secret; -o rist:// + --profile main only, "" = none */
  char rist_cname[128];   /* --cname; -o rist:// only, "" = library default */
  unsigned rist_buffer_ms; /* --buffer; -o rist:// only, 0 = library default */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* seconds >0, -1 on error */
long duration_parse(const char *s);

/* source as text */
void source_describe(const source_t *s, char *buf, size_t n);

/* -o target as text */
void out_describe(const out_target_t *o, char *buf, size_t n);

#endif
