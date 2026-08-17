/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_ARGS_H
#define DIPIFCCRET_ARGS_H

#include <stddef.h>

#include "capture/capture.h"

#define ARGS_MAX_RANGES 16

typedef struct {
  size_t range_count;
  char ranges[ARGS_MAX_RANGES][64];
  const char *range_ptrs[ARGS_MAX_RANGES]; /* points into ranges[], for capture_open */

  int listen_family; /* AF_INET or AF_INET6 */
  char listen_addr[64];
  unsigned listen_port;

  const char *iface; /* required, single Ethernet capture interface */
  size_t max_channels; /* 0 = CHANNEL_DEFAULT_MAX */
  unsigned channel_idle_timeout_s; /* 0 = reaping disabled, default 120 */
  unsigned char rtx_pt; /* shared RET/FCC RTP payload type */
  unsigned workers; /* -l socket worker threads, 0 = CPU cores */
  const char *user; /* NULL = no privilege drop */
  int verbose;
  int daemonize; /* -d, --daemonize: fork to background after startup */
  int color_mode;

  int no_ret; /* --no-ret */
  unsigned buffer_ms; /* -B, RET ring depth */
  unsigned ff_port; /* -F, 0 = reuse original channel's port */
  int no_mc_ret; /* --no-mc-ret */
  size_t max_ret_clients; /* --max-ret-clients, unicast RTX per-client seq table cap, F.3.2.1 */
  unsigned ret_client_idle_timeout_s; /* --ret-client-idle-timeout, 0 = never reap */
  int no_rsi; /* --no-rsi */
  unsigned rsi_interval_s; /* --rsi-interval */
  int rsi_mc_ret; /* --rsi-mc-ret, requires !no_mc_ret, matches dipisds --ret-rsi-mc-ret */
  char rsi_hostname[254]; /* --rsi-hostname, empty = SRBT 0 (IPv4 addr). set = SRBT 2 (DNS name) instead */

  int no_fcc; /* --no-fcc */
  unsigned gop_cap_ms; /* -G */
  size_t max_bursts; /* -C */
  double burst_multiplier; /* -X */
  unsigned duration_cap_ms; /* -D */
  unsigned max_buffer_fill_bound_ms; /* --max-buffer-fill-bound, 0 = no bound */

  int fcc_resolve_by_port; /* --fcc-resolve-by-port */
  unsigned fcc_resolve_base_port; /* --fcc-resolve-base-port, 0 = listen_port + 1 */
  unsigned congestion_nack_threshold; /* --congestion-nack-threshold, 0 = disabled, default 5 */

  cidr_t fcc_ranges[ARGS_MAX_RANGES]; /* --fcc-range, empty = every -g channel eligible (506) */
  size_t fcc_range_count;
  cidr_t fcc_client_ranges[ARGS_MAX_RANGES]; /* --fcc-client-range, empty = every client eligible (505) */
  size_t fcc_client_range_count;
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

#endif
