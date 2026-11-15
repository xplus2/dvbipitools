/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "lib/helper/argutil.h"
#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const yamlcfg_opt_t opts[] = {
    {OPT_H3_ALTSVC_PORT, "h3.altsvc-port", "YS000636", "invalid --h3-altsvc-port: %s (1..65535)", 0},
    {OPT_H3_MAX_STREAMS, "h3.max-streams", "YS000637", "invalid --h3-max-streams: %s (4..1000)", 0},
    {OPT_H3_MAX_CONNS, "h3.max-conns", "YS000638", "invalid --h3-max-conns: %s (1..65536)", 0},
    {OPT_H3_IDLE_TIMEOUT, "h3.idle-timeout", "YS000639", "invalid --h3-idle-timeout: %s (seconds, 1..86400)", 0},
    {OPT_H3_MAX_UDP_PAYLOAD, "h3.max-udp-payload", "YS000641", "invalid --h3-max-udp-payload: %s (1200..65507)", 0},
    {OPT_H3_WINDOW, "h3.window", "YS000642", "invalid --h3-window: %s (KiB, 16..1048576)", 0},
};

args_status_t dixy_opt_http(dixy_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), dixy_cfg_keys, dixy_cfg_nkeys, cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1:
      args_free(cfg);
      return ARGS_ERR;
    default: break;
  }
  switch (c) {
    case OPT_NO_HTTP2:
      cfg->no_http2 = 1;
      break;
    case OPT_NO_HTTP3:
      cfg->no_http3 = 1;
      break;
    case OPT_H3_RETRY: {
      char err[96];
      if (dixy_cfg_set_h3_retry(cfg, optarg, err, sizeof err)) {
        argerr("YS000640", "invalid --h3-retry: %s", err);
        args_free(cfg);
        return ARGS_ERR;
      }
      break;
    }
    case OPT_H3_CC: {
      char err[96];
      if (dixy_cfg_set_h3_cc(cfg, optarg, err, sizeof err)) {
        argerr("YS000643", "invalid --h3-cc: %s", err);
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
    case OPT_WS_ALLOWED_HOSTS:
      cfg->ws_allowed_hosts = optarg;
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
