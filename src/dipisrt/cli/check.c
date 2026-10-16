/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"
#include "lib/net/plain_endpoint.h"

#include "priv.h"

args_status_t srt_cli_check(config_t *cfg) {
  const endpoint_t *srt_ep;
  plain_endpoint_t *ne;

  if (!cfg->n_in) {
    argerr("HS000110", "missing -i input");
    return ARGS_ERR;
  }
  if (!cfg->n_out) {
    argerr("HS000141", "missing -o output");
    return ARGS_ERR;
  }
  if (cfg->in.is_srt == cfg->out.is_srt) {
    argerr("HS000232", "exactly one of -i/-o must be srt://, the other a plain endpoint");
    return ARGS_ERR;
  }
  srt_ep = cfg->in.is_srt ? &cfg->in : &cfg->out;
  if (srt_ep->n_srt > 1 && cfg->group_mode == SRTGROUP_NONE) {
    argerr("HS000233", "bonding several srt:// peers requires --group-mode");
    return ARGS_ERR;
  }
  if (srt_ep->n_srt == 1 && cfg->group_mode != SRTGROUP_NONE) {
    argerr("HS000234", "--group-mode has no effect with a single srt:// peer");
    return ARGS_ERR;
  }
  if (cfg->rendezvous) {
    if (srt_ep->listen) {
      argerr("HS000235", "--rendezvous is not combinable with srt://@ (listener)");
      return ARGS_ERR;
    }
    if (cfg->group_mode != SRTGROUP_NONE) {
      argerr("HS000236", "--rendezvous is not combinable with --group-mode");
      return ARGS_ERR;
    }
    if (!cfg->local_host[0] || !cfg->local_port) {
      argerr("HS000237", "--rendezvous requires --local <host:port>");
      return ARGS_ERR;
    }
  }
  if (config_local_family_mismatch(cfg)) {
    argerr("HS000722", "--local and srt:// peer differ in address family");
    return ARGS_ERR;
  }
  if (!argutil_srt_passphrase_valid(cfg->passphrase)) {
    argerr("HS000238", "--passphrase must be 10..79 characters");
    return ARGS_ERR;
  }
  if (cfg->pbkeylen && !cfg->passphrase[0]) {
    argerr("HS000239", "--pbkeylen requires --passphrase");
    return ARGS_ERR;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) return ARGS_ERR;
  if (argutil_metrics_inspect_ts_validate(TOOL_NAME, cfg->metrics_id, cfg->metrics_inspect_ts)) return ARGS_ERR;
  if (cfg->al_fec_l && !cfg->al_fec_port) {
    argerr("HS000145", "--al-fec requires --al-fec-port");
    return ARGS_ERR;
  }
  if (!cfg->al_fec_l && cfg->al_fec_port) log_code("HA000146", TOOL_NAME ": --al-fec-port has no effect without --al-fec");
  ne = cfg->in.is_srt ? &cfg->out.nonsrt : &cfg->in.nonsrt;
  if (cfg->al_fec_l && ne->kind != PLAIN_EP_RTP) log_code("HA000240", TOOL_NAME ": --al-fec has no effect, the non-srt:// side isn't rtp://");
  ne->al_fec_l = cfg->al_fec_l;
  ne->al_fec_d = cfg->al_fec_d;
  ne->al_fec_port = cfg->al_fec_port;
  if (cfg->insecure_tls && !(cfg->in.nonsrt.kind == PLAIN_EP_HTTP && cfg->in.nonsrt.http.tls)) log_code("HA000148", TOOL_NAME ": --insecure needs -i https://");
  if (cfg->pace && (cfg->in.is_srt || (cfg->in.nonsrt.kind != PLAIN_EP_FILE && cfg->in.nonsrt.kind != PLAIN_EP_HTTP)))
    log_code("HA000721", TOOL_NAME ": --pace requires -i - , a file path or http(s)://");
  if (cfg->send_buffer_mult && !config_is_sender(cfg)) log_code("HA100114", TOOL_NAME ": --send-buffer-mult needs -o srt://");
  return ARGS_OK;
}
