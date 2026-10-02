/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"
#include "lib/mux/fec2022.h"

#include "priv.h"

args_status_t dixy_opt_server(dixy_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case 'I':
      cfg->iface = optarg;
      break;
    case 'l':
      if (dixy_cfg_listen(&cfg->listen, optarg)) {
        argerr("invalid -l/--listen address: %s", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      break;
    case 'L':
      if (dixy_cfg_listen(&cfg->listen_tls, optarg)) {
        argerr("invalid -L/--listen-tls address: %s", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      break;
    case OPT_TLS_CERT:
      cfg->tls_cert = optarg;
      break;
    case OPT_TLS_KEY:
      cfg->tls_key = optarg;
      break;
    case 'j':
      if (dixy_cfg_workers(&cfg->workers_spec, optarg)) {
        argerr("invalid -j/--workers: %s (-1/-2/-3, or a positive count)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      break;
    case OPT_CPU_AFFINITY:
      if (cpuaff_parse(&cfg->cpu_affinity, optarg)) {
        argerr("invalid --cpu-affinity: %s (off, auto, or list like 2-5,8)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      break;
    case OPT_MAX_CLIENTS: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, 65536, &v)) {
        argerr("invalid --max-clients: %s (1..65536)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->max_clients = (int)v;
      break;
    }
    case OPT_MAX_CHANNELS: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, 1024, &v)) {
        argerr("invalid --max-channels: %s (1..1024)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->max_channels = (int)v;
      break;
    }
    case OPT_IDLE_TIMEOUT: {
      unsigned v;
      if (argutil_uint_range(optarg, 0, 86400, &v)) {
        argerr("invalid --idle-timeout: %s (seconds, 0..86400, 0 = off)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->idle_timeout_s = v;
      break;
    }
    case OPT_CAPTURE_RING_SIZE: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
        argerr("invalid --capture-ring-size: %s (KiB, min 1)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->capture_ring_kib = v;
      break;
    }
    case OPT_TS_STARTUP_TIMEOUT: {
      char *end;
      double v = strtod(optarg, &end);
      if (*end != '\0' || !isfinite(v) || v < 0.0 || v > 1e9) {
        argerr("invalid --ts-startup-timeout: %s (seconds, 0=off)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->ts_startup_timeout_s = v;
      break;
    }
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
