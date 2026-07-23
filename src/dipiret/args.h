/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRET_ARGS_H
#define DIPIRET_ARGS_H

#include <stddef.h>

#define ARGS_MAX_RANGES 16

typedef struct {
  size_t range_count;
  char ranges[ARGS_MAX_RANGES][64];
  const char *range_ptrs[ARGS_MAX_RANGES]; /* points into ranges[], for capture_open */

  int listen_family; /* AF_INET or AF_INET6 */
  char listen_addr[64];
  unsigned listen_port;

  const char *iface; /* NULL = libpcap "any" */
  const char *bpf_expr; /* NULL = auto-built from ranges */
  unsigned buffer_ms;
  size_t max_channels; /* 0 = CHANNEL_DEFAULT_MAX */
  unsigned char rtx_pt;
  unsigned ff_port; /* 0 = reuse the original channel's port */
  int no_mc_ret;
  unsigned workers; /* 0 = online CPU count, min 1 */
  const char *user; /* NULL = no privilege drop */
  int verbose;
  int color_mode;
} config_t;

typedef enum { ARGS_OK, ARGS_HELP, ARGS_ERR } args_status_t;

args_status_t args_parse(int argc, char **argv, config_t *cfg);

#endif
