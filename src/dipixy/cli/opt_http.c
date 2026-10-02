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

args_status_t dixy_opt_http(dixy_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case OPT_NO_HTTP2:
      cfg->no_http2 = 1;
      break;
    case OPT_NO_HTTP3:
      cfg->no_http3 = 1;
      break;
    case OPT_H3_ALTSVC_PORT: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, 65535, &v)) {
        argerr("invalid --h3-altsvc-port: %s (1..65535)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->h3_altsvc_port = v;
      break;
    }
    case OPT_H3_MAX_STREAMS: {
      unsigned v;
      if (argutil_uint_range(optarg, 4, 1000, &v)) {
        argerr("invalid --h3-max-streams: %s (4..1000)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->h3_max_streams = v;
      break;
    }
    case OPT_H3_MAX_CONNS: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, 65536, &v)) {
        argerr("invalid --h3-max-conns: %s (1..65536)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->h3_max_conns = v;
      break;
    }
    case OPT_H3_IDLE_TIMEOUT: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, 86400, &v)) {
        argerr("invalid --h3-idle-timeout: %s (seconds, 1..86400)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->h3_idle_s = v;
      break;
    }
    case OPT_H3_RETRY: {
      char err[96];
      if (dixy_cfg_set_h3_retry(cfg, optarg, err, sizeof err)) {
        argerr("invalid --h3-retry: %s", err);
        args_free(cfg);
        return ARGS_ERR;
      }
      break;
    }
    case OPT_H3_MAX_UDP_PAYLOAD: {
      unsigned v;
      if (argutil_uint_range(optarg, 1200, 65507, &v)) {
        argerr("invalid --h3-max-udp-payload: %s (1200..65507)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->h3_max_udp = v;
      break;
    }
    case OPT_H3_WINDOW: {
      unsigned v;
      if (argutil_uint_range(optarg, 16, 1048576, &v)) {
        argerr("invalid --h3-window: %s (KiB, 16..1048576)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->h3_window_kib = v;
      break;
    }
    case OPT_H3_CC: {
      char err[96];
      if (dixy_cfg_set_h3_cc(cfg, optarg, err, sizeof err)) {
        argerr("invalid --h3-cc: %s", err);
        args_free(cfg);
        return ARGS_ERR;
      }
      break;
    }
    case OPT_AUTH:
      if (dixy_basic_auth_parse("--auth", optarg, cfg->http_auth, sizeof cfg->http_auth)) {
        args_free(cfg);
        return ARGS_ERR;
      }
      break;
    case OPT_METRICS_AUTH:
      if (dixy_basic_auth_parse("--metrics-auth", optarg, cfg->http_metrics_auth, sizeof cfg->http_metrics_auth)) {
        args_free(cfg);
        return ARGS_ERR;
      }
      break;
    case OPT_CORS_ORIGIN:
      cfg->cors_origins = optarg;
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
