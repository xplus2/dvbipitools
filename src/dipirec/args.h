/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_ARGS_H
#define DIPIREC_ARGS_H

#include <stddef.h>

#include "lib/net/httpclient/httpclient.h"

typedef enum {
  URI_RTP,  /* multicast, RTP wrapped */
  URI_UDP,  /* multicast, plain ts */
  URI_HTTP, /* http:// or https://, http_url_t.tls tells which */
  URI_FILE, /* "-" = stdin, RTP-vs-raw auto-detected */
  URI_RIST, /* single peer, @ required (listen) */
  URI_SRT   /* single peer, no bonding/rendezvous (see dipisrt) */
} uri_kind_t;

typedef struct {
  uri_kind_t kind;
  int rtp_wrapped; /* RTP payload. URI_RTP / URI_UDP only, protocol-inherent */
  /* URI_RTP / URI_UDP */
  int family; /* AF_INET or AF_INET6 */
  char group[64];
  unsigned port;
  /* URI_HTTP */
  http_url_t http;
  /* URI_FILE, "" means stdin */
  char file_path[512];
  /* URI_RIST, stored raw for librist's own parser */
  char rist_uri[256];
  /* URI_SRT */
  int srt_family; /* AF_INET or AF_INET6, display only */
  char srt_host[64];
  unsigned srt_port;
  int srt_listen; /* 1 = @, bind/listen/accept. 0 = call out (connect) */
} source_t;

typedef enum { FMT_RAW, FMT_TS, FMT_MKV, FMT_MKA, FMT_MP4, FMT_M4A } out_fmt_t;
typedef enum { SUB_KEEP, SUB_STRIP, SUB_SRT } sub_mode_t;
typedef enum { PMT_SEL_AUTO, PMT_SEL_PID, PMT_SEL_ALL } pmt_sel_t;
typedef enum { OUT_FILE, OUT_RTP, OUT_UDP, OUT_RIST, OUT_RTMP, OUT_RTMPS, OUT_SRT } out_kind_t;
typedef enum { RIST_PROF_SIMPLE, RIST_PROF_MAIN } rist_profile_sel_t;

#define DIPIREC_MAX_OUT 8

typedef struct {
  out_kind_t kind;
  /* OUT_RTP / OUT_UDP */
  int family; /* AF_INET or AF_INET6 */
  char group[64];
  unsigned port;
  /* OUT_FILE, "-" = stdout */
  char file_path[512];
  /* OUT_RIST, stored raw for librist's own parser */
  char rist_uri[256];
  /* OUT_RTMP / OUT_RTMPS, passed to rtmpout's own parser as-is */
  char rtmp_url[600];
  /* OUT_SRT: one peer per target, not bonded (repeat -o for more), always calls out */
  int srt_family; /* AF_INET or AF_INET6, display only */
  char srt_host[64];
  unsigned srt_port;
} out_target_t;

typedef struct {
  int enabled;
  int family;      /* AF_INET or AF_INET6 */
  char addr[64];   /* RET server unicast address */
  unsigned port;
  int mc_enabled;  /* MC repair session, on by default, --no-ret-mc clears */
  unsigned mc_port; /* --ret-mc-port, 0 reuses -i's port, F.6.2.2 */
  unsigned char rtx_pt; /* --ret-pt, must match RET server's -R */
  unsigned wait_ms; /* --ret-wait, ms to hold after a NACK before giving up */
} ret_cfg_t;

typedef struct {
  out_target_t out[DIPIREC_MAX_OUT]; /* -o, repeatable */
  int n_out;
  source_t source;      /* -i */
  int audio_all;        /* -a all */
  unsigned audio_track; /* -a N, 1-based, !audio_all */
  out_fmt_t format;     /* -f resolved */
  pmt_sel_t pmt_sel;    /* -p, AUTO if not given */
  unsigned pmt_pid;     /* -p <pid>, valid iff pmt_sel == PMT_SEL_PID */
  sub_mode_t subs;      /* -s */
  long duration_s;      /* -t seconds, 0 = until stopped */
  const char *iface_in;  /* -I, -i rtp/udp join iface, NULL = kernel default */
  const char *iface_out; /* --out-iface, -o rtp/udp send iface, NULL = kernel default */
  int out_ttl;           /* --ttl, -o rtp/udp send TTL/hop-limit, 0 = kernel default (1) */
  int verbose;          /* -v */
  long sub_lead_ms;     /* --sub-lead, shifts subtitles earlier */
  int color_mode;       /* int, actually a log_color_t */
  ret_cfg_t ret;        /* --ret and friends, RTP source only */
  int pace;             /* --pace, URI_FILE source only */
  unsigned strip_mask;  /* --strip, STRIP_* bits from filter/ts.h, -f ts only */
  rist_profile_sel_t rist_profile; /* --profile, -o rist:// only */
  char rist_secret[128];  /* --secret, -o rist:// + --profile main only, "" = none */
  char rist_cname[128];   /* --cname, -o rist:// only, "" = library default */
  unsigned rist_buffer_ms; /* --buffer, -o rist:// only, 0 = library default */
  rist_profile_sel_t rist_profile_in; /* --profile-in, -i rist:// only */
  int insecure_tls;              /* --insecure, -o rtmps:// or -i https:// */
  const char *metrics_sock;      /* --metrics. NULL = default socket path */
  const char *metrics_id;        /* --metrics-id. NULL = metrics disabled */
  unsigned metrics_interval_s;   /* --metrics-interval. 0 = default */
  char srt_passphrase_in[128];   /* --srt-passphrase-in, -i srt:// only. "" = no encryption */
  int srt_pbkeylen_in;           /* --srt-pbkeylen-in, requires --srt-passphrase-in. 0 = library default (16) */
  char srt_streamid_in[128];     /* --srt-streamid-in, -i srt:// only. "" = none */
  char srt_packetfilter_in[256]; /* --srt-packetfilter-in, -i srt:// only. "" = none */
  unsigned srt_latency_in_ms;    /* --srt-latency-in, -i srt:// only. 0 = library default */
  char srt_passphrase[128];      /* --srt-passphrase, applies to every -o srt:// target. "" = no encryption */
  int srt_pbkeylen;              /* --srt-pbkeylen, requires --srt-passphrase. 0 = library default (16) */
  char srt_streamid[128];        /* --srt-streamid, applies to every -o srt:// target. "" = none */
  char srt_packetfilter[256];    /* --srt-packetfilter, applies to every -o srt:// target. "" = none */
  unsigned srt_latency_ms;       /* --srt-latency, applies to every -o srt:// target. 0 = library default */
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
