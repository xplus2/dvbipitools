/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_ARGS_H
#define DIPIFCCRET_ARGS_H

#include <stddef.h>

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
  int color_mode;

  int no_ret; /* --no-ret */
  unsigned buffer_ms; /* -B, RET ring depth */
  unsigned ff_port; /* -F, 0 = reuse the original channel's port */
  int no_mc_ret; /* --no-mc-ret */

  int no_fcc; /* --no-fcc */
  unsigned gop_cap_ms; /* -G */
  size_t max_bursts; /* -C */
  double burst_multiplier; /* -X */
  unsigned duration_cap_ms; /* -D */
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

#endif
