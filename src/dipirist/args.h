/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRIST_ARGS_H
#define DIPIRIST_ARGS_H

#include <stddef.h>

#include "lib/net/plain_endpoint.h"

#define DIPIRIST_MAX_PEERS 8

typedef struct {
  int is_rist; /* 1: rist://, repeatable up to DIPIRIST_MAX_PEERS for bonding. 0: nonrist */
  char rist_uri[DIPIRIST_MAX_PEERS][256];
  int n_rist;
  plain_endpoint_t nonrist; /* valid iff !is_rist */
} endpoint_t;

typedef enum { RIST_PROF_SIMPLE, RIST_PROF_MAIN } rist_profile_sel_t;

typedef struct {
  endpoint_t in;  /* -i */
  endpoint_t out; /* -o */
  rist_profile_sel_t profile;
  char secret[128];   /* "" = none; --profile main only */
  char cname[128];    /* "" = library default */
  unsigned buffer_ms; /* recovery_length_min/max on every peer. 0 = library default */
  const char *iface;  /* non-RIST side multicast join/send interface. NULL = kernel default */
  unsigned al_fec_l;
  unsigned al_fec_d;
  unsigned al_fec_port;
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

/* 1 if -o is rist:// (this process sends into RIST), 0 if -i is (receives from RIST) */
int config_is_sender(const config_t *cfg);

void endpoint_describe(const endpoint_t *e, char *buf, size_t n);

#endif
