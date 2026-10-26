/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"
#include "priv.h"

args_status_t dscr_cli_check(const config_t *cfg) {
  int has_rtmps;
  int has_rtmp;
  int n_file;
  int has_srt_out;

  if (!cfg->have_input) {
    argerr("DS000110", "missing -i input");
    return ARGS_ERR;
  }
  if (!cfg->n_out) {
    argerr("DS000141", "missing -o output");
    return ARGS_ERR;
  }
  has_rtmps = 0;
  has_rtmp = 0;
  n_file = 0;
  for (int i = 0; i < cfg->n_out; i++) {
    switch (cfg->out[i].kind) {
      case OUT_FILE: n_file++; break;
      case OUT_RTMPS: has_rtmps = 1; has_rtmp = 1; break;
      case OUT_RTMP: has_rtmp = 1; break;
      case OUT_SRT: break;
    }
  }
  if ((cfg->format == FMT_MKV || cfg->format == FMT_MKA) && n_file != 1) {
    argerr("DS000330", "-f mkv/mka requires exactly one -o file target (plus optional rtmp(s) targets)");
    return ARGS_ERR;
  }
  if (cfg->insecure_tls && !has_rtmps && !cfg->unicast_emm_uri) log_code("DA000331", TOOL_NAME ": --insecure has no effect without -u or -o rtmps://");
  if (cfg->strip_lcevc && cfg->format == FMT_TS && !has_rtmp) log_code("DA000332", TOOL_NAME ": --strip-lcevc has no effect, no -f mkv/mka or -o rtmp(s):// target");
  if (cfg->biss2_sw_given && cfg->biss2_esw_given) {
    argerr("DS000333", "--biss2-sw and --biss2-esw are mutually exclusive");
    return ARGS_ERR;
  }
  if (cfg->biss2_esw_given != cfg->biss2_id_given) {
    argerr("DS000334", "--biss2-esw and --biss2-id must be given together");
    return ARGS_ERR;
  }
  if (cfg->biss1_sw_given && (cfg->biss2_sw_given || cfg->biss2_esw_given)) {
    argerr("DS000335", "--biss1-sw is mutually exclusive with --biss2-sw/--biss2-esw");
    return ARGS_ERR;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) return ARGS_ERR;
  if (argutil_metrics_inspect_ts_validate(TOOL_NAME, cfg->metrics_id, cfg->metrics_inspect_ts)) return ARGS_ERR;
  if (cfg->metrics_n_known_pids && cfg->metrics_inspect_ts != METRICS_INSPECT_TS_FULL) {
    argutil_err(TOOL_NAME, "DS000336", "--metrics-inspect-ts-pids requires --metrics-inspect-ts full");
    return ARGS_ERR;
  }
  if (cfg->profile_given && cfg->input.kind != INPUT_RIST) log_code("DA000337", TOOL_NAME ": --rist-profile has no effect, -i isn't rist://");
  if (cfg->rist_key_size && cfg->input.kind != INPUT_RIST) log_code("DA000338", TOOL_NAME ": --rist-encryption-type has no effect, -i isn't rist://");
  if (cfg->rist_key_size && cfg->input.kind == INPUT_RIST && !cfg->rist_profile_main) {
    argerr("DS000339", "--rist-encryption-type requires --rist-profile main");
    return ARGS_ERR;
  }
  if (argutil_srt_passphrase_opt(TOOL_NAME, cfg->srt_passphrase_in, "--srt-passphrase-in")) return ARGS_ERR;
  if (cfg->srt_pbkeylen_in && !cfg->srt_passphrase_in[0]) {
    argerr("DS000340", "--srt-pbkeylen-in requires --srt-passphrase-in");
    return ARGS_ERR;
  }
  if (cfg->input.kind != INPUT_SRT && (cfg->srt_passphrase_in[0] || cfg->srt_pbkeylen_in || cfg->srt_streamid_in[0] || cfg->srt_packetfilter_in[0] || cfg->srt_latency_in_ms))
    log_code("DA000341", TOOL_NAME ": --srt-*-in has no effect, -i isn't srt://");
  if (argutil_srt_passphrase_opt(TOOL_NAME, cfg->srt_passphrase, "--srt-passphrase")) return ARGS_ERR;
  if (cfg->srt_pbkeylen && !cfg->srt_passphrase[0]) {
    argerr("DS000342", "--srt-pbkeylen requires --srt-passphrase");
    return ARGS_ERR;
  }
  has_srt_out = 0;
  for (int i = 0; i < cfg->n_out; i++) {
    if (cfg->out[i].kind == OUT_SRT) has_srt_out = 1;
  }
  if (!has_srt_out && (cfg->srt_passphrase[0] || cfg->srt_pbkeylen || cfg->srt_streamid[0] || cfg->srt_packetfilter[0] || cfg->srt_latency_ms))
    log_code("DA000343", TOOL_NAME ": --srt-* has no effect without -o srt://");
  return ARGS_OK;
}
