/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"

#include "priv.h"

args_status_t dixy_cli_check(config_t *cfg) {
  if (cfg->tls_cert && !cfg->tls_key) {
    argerr("--tls-cert given without --tls-key");
    args_free(cfg);
    return ARGS_ERR;
  }
  if (cfg->tls_key && !cfg->tls_cert) {
    argerr("--tls-key given without --tls-cert");
    args_free(cfg);
    return ARGS_ERR;
  }
  if (cfg->hls_part_size >= cfg->segment_size) {
    argerr("--hls-part-size (%.2f) must be smaller than --segment-size (%.2f)", cfg->hls_part_size, cfg->segment_size);
    args_free(cfg);
    return ARGS_ERR;
  }
  if (cfg->dash_part_size >= cfg->segment_size) {
    argerr("--dash-part-size (%.2f) must be smaller than --segment-size (%.2f)", cfg->dash_part_size, cfg->segment_size);
    args_free(cfg);
    return ARGS_ERR;
  }
  if ((double)cfg->ssdp_max_age_s < 2.0 * cfg->ssdp_interval_s) {
    argerr("--ssdp-max-age (%u) must be at least 2x --ssdp-interval (%.2f)", cfg->ssdp_max_age_s, cfg->ssdp_interval_s);
    args_free(cfg);
    return ARGS_ERR;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) {
    args_free(cfg);
    return ARGS_ERR;
  }
  if (argutil_metrics_inspect_ts_validate(TOOL_NAME, cfg->metrics_id, cfg->metrics_inspect_ts)) {
    args_free(cfg);
    return ARGS_ERR;
  }
  if (cfg->enable_dlna && cfg->no_spts) {
    argerr("--enable-dlna requires spts in -f/--format (DLNA playback always uses /spts)");
    args_free(cfg);
    return ARGS_ERR;
  }
  if (cfg->enable_dlna && cfg->no_rawaudio) {
    argerr("--enable-dlna requires rawaudio in -f/--format (DLNA radio items use /rawaudio)");
    args_free(cfg);
    return ARGS_ERR;
  }
  if (cfg->enable_dlna) {
    if (cfg->dlna_host_opt) {
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->dlna_host, sizeof cfg->dlna_host, cfg->dlna_host_opt, "--dlna-host")) {
        args_free(cfg);
        return ARGS_ERR;
      }
    } else if (cfg->listen.scope != LISTEN_ANY) {
      char portbuf[12];
      size_t off;
      uint_to_str(portbuf, cfg->listen.port);
      if (cfg->listen.scope == LISTEN_V6) {
        off = bufcpy(cfg->dlna_host, sizeof cfg->dlna_host, "[");
        off += bufcpy(cfg->dlna_host + off, sizeof cfg->dlna_host - off, cfg->listen.addr);
        off += bufcpy(cfg->dlna_host + off, sizeof cfg->dlna_host - off, "]:");
      } else {
        off = bufcpy(cfg->dlna_host, sizeof cfg->dlna_host, cfg->listen.addr);
        off += bufcpy(cfg->dlna_host + off, sizeof cfg->dlna_host - off, ":");
      }
      bufcpy(cfg->dlna_host + off, sizeof cfg->dlna_host - off, portbuf);
    } else {
      argerr("--enable-dlna needs --dlna-host (or a concrete -l/--listen address, not 'all')");
      args_free(cfg);
      return ARGS_ERR;
    }
  }
  return ARGS_OK;
}
