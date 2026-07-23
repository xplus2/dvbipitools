/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_ARGS_H
#define DIPIREC_ARGS_H

#include <stddef.h>

typedef enum {
  URI_RTP,  /* multicast, RTP wrapped */
  URI_UDP,  /* multicast, plain ts */
  URI_UDPXY /* udpxy http */
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
} source_t;

typedef enum { FMT_RAW, FMT_TS, FMT_MKV, FMT_MKA } out_fmt_t;
typedef enum { SUB_KEEP, SUB_STRIP, SUB_SRT } sub_mode_t;

typedef struct {
  int enabled;     /* --ret given; RET client off otherwise, no behavior change */
  int family;      /* AF_INET or AF_INET6 */
  char addr[64];   /* RET server unicast address */
  unsigned port;
  int mc_enabled;  /* join the MC repair session; default on, --no-ret-mc clears */
  unsigned mc_port; /* --ret-mc-port; 0 = reuse -i's port, per F.6.2.2 */
  unsigned char rtx_pt; /* --ret-pt; must match the RET server's -R */
  unsigned wait_ms; /* --ret-wait; hold budget after a NACK before giving up on a gap */
} ret_cfg_t;

typedef struct {
  const char *out_path; /* -o; "-" = stdout */
  source_t source;      /* -i */
  int audio_all;        /* -a all */
  unsigned audio_track; /* -a N, 1-based, !audio_all */
  out_fmt_t format;     /* -f resolved */
  sub_mode_t subs;      /* -s */
  long duration_s;      /* -t seconds; 0 = until stopped */
  const char *iface;    /* -I; NULL = kernel default */
  int verbose;          /* -v */
  long sub_lead_ms;     /* --sub-lead; subtitles shifted earlier */
  int color_mode;       /* --color; log_color_t */
  ret_cfg_t ret;        /* --ret and friends; RTP source only */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* seconds >0, -1 on error */
long duration_parse(const char *s);

/* source as text */
void source_describe(const source_t *s, char *buf, size_t n);

#endif
