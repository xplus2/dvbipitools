/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISRT_ARGS_H
#define DIPISRT_ARGS_H

#include <stddef.h>

#include "lib/net/plain_endpoint.h"
#include "lib/net/srt/srtcommon.h"

typedef struct {
  int is_srt;  /* 1: srt://, repeatable up to SRTCOMMON_MAX_PEERS for bonding. 0: nonsrt */
  int listen;  /* @ prefix seen: this side binds/listens/accepts. else: calls out. same for every bonded peer */
  int family[SRTCOMMON_MAX_PEERS];  /* AF_INET or AF_INET6; display only, see endpoint_describe() */
  char srt_host[SRTCOMMON_MAX_PEERS][64]; /* numeric IP; argutil_addrport_parse doesn't resolve hostnames */
  unsigned srt_port[SRTCOMMON_MAX_PEERS];
  int n_srt;
  plain_endpoint_t nonsrt; /* valid iff !is_srt */
} endpoint_t;

typedef struct {
  endpoint_t in;  /* -i */
  endpoint_t out; /* -o */
  srtgroup_mode_t group_mode; /* NONE unless srt:// side bonds (n_srt > 1) */
  int rendezvous;             /* srt:// side uses srt_rendezvous(); not combinable with @ or bonding */
  char local_host[64]; /* --local host:port; required with --rendezvous */
  unsigned local_port;
  char passphrase[128]; /* "" = no encryption; else 10..79 chars, SRT's own PBKDF2 passphrase bounds */
  int pbkeylen;         /* 0 = library default (16); else 16/24/32 */
  char streamid[128];   /* "" = none */
  char packetfilter[256]; /* "" = none; raw SRTO_PACKETFILTER string, e.g. "fec,cols:10,rows:5" */
  unsigned latency_ms;    /* 0 = library default */
  unsigned send_buffer_mult; /* --send-buffer-mult. 0 = default 4; clamped 1..32 */
  const char *iface;      /* non-SRT side multicast join/send interface. NULL = kernel default */
  unsigned al_fec_l, al_fec_d, al_fec_port;
  int verbose;
  int daemonize; /* -d, --daemonize: fork to background after startup */
  int color_mode;
  int insecure_tls; /* -k, --insecure, -i https:// source only */
  const char *metrics_sock;    /* --metrics. NULL = default socket path */
  const char *metrics_id;      /* --metrics-id. NULL = metrics disabled */
  unsigned metrics_interval_s; /* --metrics-interval. 0 = default */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

/* 1 if -o is srt:// (this process sends into SRT), 0 if -i is (receives from SRT) */
int config_is_sender(const config_t *cfg);

void endpoint_describe(const endpoint_t *e, char *buf, size_t n);

#endif
