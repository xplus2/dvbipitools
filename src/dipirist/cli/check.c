/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"
#include "lib/net/plain_endpoint.h"

#include "priv.h"

args_status_t rist_cli_check(config_t *cfg) {
  plain_endpoint_t *ne;

  if (!cfg->n_in) {
    argerr("VS000110", "missing -i input");
    return ARGS_ERR;
  }
  if (!cfg->n_out) {
    argerr("VS000141", "missing -o output");
    return ARGS_ERR;
  }
  if (cfg->in.is_rist == cfg->out.is_rist) {
    argerr("VS000142", "exactly one of -i/-o must be rist://, the other a plain endpoint");
    return ARGS_ERR;
  }
  if (cfg->secret[0] && cfg->profile != RIST_PROF_MAIN) {
    argerr("VS000143", "--secret requires --profile main");
    return ARGS_ERR;
  }
  if (cfg->key_size && cfg->profile != RIST_PROF_MAIN) {
    argerr("VS000144", "--encryption-type requires --profile main");
    return ARGS_ERR;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) return ARGS_ERR;
  if (argutil_metrics_inspect_ts_validate(TOOL_NAME, cfg->metrics_id, cfg->metrics_inspect_ts)) return ARGS_ERR;
  if (cfg->al_fec_l && !cfg->al_fec_port) {
    argerr("VS000145", "--al-fec requires --al-fec-port");
    return ARGS_ERR;
  }
  if (!cfg->al_fec_l && cfg->al_fec_port) log_code("VA000146", TOOL_NAME ": --al-fec-port has no effect without --al-fec");

  ne = cfg->in.is_rist ? &cfg->out.nonrist : &cfg->in.nonrist;
  if (cfg->al_fec_l && ne->kind != PLAIN_EP_RTP) log_code("VA000147", TOOL_NAME ": --al-fec has no effect, non-rist:// side isn't rtp://");
  ne->al_fec_l = cfg->al_fec_l;
  ne->al_fec_d = cfg->al_fec_d;
  ne->al_fec_port = cfg->al_fec_port;
  if (cfg->insecure_tls && !(cfg->in.nonrist.kind == PLAIN_EP_HTTP && cfg->in.nonrist.http.tls))
    log_code("VS000148", TOOL_NAME ": --insecure needs -i https://");
  return ARGS_OK;
}
