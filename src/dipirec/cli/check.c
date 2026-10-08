/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"

#include "priv.h"
#include "../filter/ts.h"

args_status_t rec_cli_check(config_t *cfg) {
  int n_rist_out;
  int has_rtp_udp;
  int has_rtp;
  int has_rtmp;
  int has_rtmps;
  int has_non_file;
  int n_non_rtmp;
  int has_rist;
  int has_srt_out;

  if (!cfg->n_out) {
    argerr("missing -o output");
    return ARGS_ERR;
  }
  if (!cfg->fl.have_in) {
    argerr("missing -i input");
    return ARGS_ERR;
  }
  n_rist_out = 0;
  for (int i = 0; i < cfg->n_out; i++) {
    if (cfg->out[i].kind == OUT_RIST) n_rist_out++;
  }
  if (n_rist_out > 1) {
    argerr("at most one -o rist:// target: librist isn't safe with more than one context per process");
    return ARGS_ERR;
  }
  if (cfg->source.kind == URI_RIST && n_rist_out) {
    argerr("-i rist:// and -o rist:// cannot combine: librist isn't safe with more than one context per process");
    return ARGS_ERR;
  }
  if (cfg->ret.enabled && cfg->source.kind != URI_RTP) {
    argerr("--ret requires -i rtp://, no RTP sequence numbers otherwise");
    return ARGS_ERR;
  }
  if (cfg->ret.enabled && cfg->ret.mc_enabled && cfg->ret.family != cfg->source.family) {
    argerr("--ret family must match -i's for the SSM repair join; use --no-ret-mc otherwise");
    return ARGS_ERR;
  }
  if (cfg->pace && cfg->source.kind != URI_FILE) {
    argerr("--pace requires -i - or -i <path>");
    return ARGS_ERR;
  }
  if (!cfg->fl.have_format) {
    cfg->format = FMT_TS;
    for (int i = 0; i < cfg->n_out; i++) {
      if (cfg->out[i].kind == OUT_FILE && strcmp(cfg->out[i].file_path, "-") != 0) {
        rec_fmt_from_suffix(cfg->out[i].file_path, &cfg->format);
        break;
      }
    }
  }
  has_rtp_udp = 0;
  has_rtp = 0;
  has_rtmp = 0;
  has_rtmps = 0;
  has_non_file = 0;
  n_non_rtmp = 0;
  for (int i = 0; i < cfg->n_out; i++) {
    out_kind_t k = cfg->out[i].kind;
    if (k == OUT_RTP || k == OUT_UDP) has_rtp_udp = 1;
    if (k == OUT_RTP) has_rtp = 1;
    if (k == OUT_RTMP || k == OUT_RTMPS) {
      has_rtmp = 1;
    } else {
      n_non_rtmp++;
      if (k != OUT_FILE) has_non_file = 1;
    }
    if (k == OUT_RTMPS) has_rtmps = 1;
  }
  if ((cfg->format == FMT_MKV || cfg->format == FMT_MKA || cfg->format == FMT_MP4 || cfg->format == FMT_M4A) && (has_non_file || n_non_rtmp != 1)) {
    argerr("-f mkv/mka/mp4/m4a requires exactly one -o file target (plus optional rtmp(s) targets)");
    return ARGS_ERR;
  }
  if (cfg->format == FMT_RAW && has_rtmp) {
    argerr("-f raw is incompatible with an -o rtmp://rtmps:// target");
    return ARGS_ERR;
  }
  if (cfg->iface_out && !has_rtp_udp) log_line(TOOL_NAME ": --out-iface needs -o rtp:// or udp:// target");
  if (cfg->out_ttl && !has_rtp_udp) log_line(TOOL_NAME ": --ttl needs -o rtp:// or udp:// target");
  if (cfg->al_fec_l && !cfg->al_fec_port) {
    argerr("--al-fec requires --al-fec-port");
    return ARGS_ERR;
  }
  if (!cfg->al_fec_l && cfg->al_fec_port) log_line(TOOL_NAME ": --al-fec-port needs --al-fec");
  if (cfg->al_fec_l && !has_rtp && cfg->source.kind != URI_RTP) log_line(TOOL_NAME ": --al-fec needs -i rtp:// or -o rtp:// target");
  if (cfg->insecure_tls && !has_rtmps && !(cfg->source.kind == URI_HTTP && cfg->source.http.tls)) log_line(TOOL_NAME ": --insecure needs -o rtmps:// target or -i https:// source");
  /* LCEVC also strips mp4/mkv/rtmp inline data, unlike others */
  if (cfg->fl.have_strip && cfg->format != FMT_TS && (cfg->strip_mask & ~STRIP_LCEVC)) log_line(TOOL_NAME ": --strip has no effect outside -f ts");
  if (cfg->subs == SUB_SRT && cfg->format != FMT_MKV && cfg->format != FMT_MKA && cfg->format != FMT_MP4 && cfg->format != FMT_M4A) {
    argerr("-s srt requires -f mkv, mka, mp4 or m4a");
    return ARGS_ERR;
  }
  has_rist = 0;
  for (int i = 0; i < cfg->n_out; i++) {
    if (cfg->out[i].kind == OUT_RIST) has_rist = 1;
  }
  if (!has_rist && (cfg->fl.have_profile || cfg->fl.have_secret || cfg->fl.have_cname || cfg->fl.have_buffer || cfg->rist_key_size)) log_line(TOOL_NAME ": --rist-profile/--rist-secret/--rist-encryption-type/--rist-cname/--rist-buffer need -o rist:// target");
  if (has_rist && cfg->fl.have_secret && cfg->rist_profile != RIST_PROF_MAIN) {
    argerr("--rist-secret requires --rist-profile main");
    return ARGS_ERR;
  }
  if (has_rist && cfg->rist_key_size && cfg->rist_profile != RIST_PROF_MAIN) {
    argerr("--rist-encryption-type requires --rist-profile main");
    return ARGS_ERR;
  }
  if (cfg->source.kind == URI_RIST && cfg->rist_key_size_in && cfg->rist_profile_in != RIST_PROF_MAIN) {
    argerr("--rist-encryption-type-in requires --rist-profile-in main");
    return ARGS_ERR;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) return ARGS_ERR;
  if (argutil_metrics_inspect_ts_validate(TOOL_NAME, cfg->metrics_id, cfg->metrics_inspect_ts)) return ARGS_ERR;
  if (cfg->metrics_n_known_pids && cfg->metrics_inspect_ts != METRICS_INSPECT_TS_FULL) {
    argutil_err(TOOL_NAME, "--metrics-inspect-ts-pids requires --metrics-inspect-ts full");
    return ARGS_ERR;
  }
  if ((cfg->fl.have_profile_in || cfg->rist_key_size_in) && cfg->source.kind != URI_RIST) log_line(TOOL_NAME ": --rist-profile-in/--rist-encryption-type-in need -i rist:// source");
  if (rec_validate_srt_passphrase(cfg->srt_passphrase_in, cfg->srt_pbkeylen_in, "-in")) return ARGS_ERR;
  if (cfg->source.kind != URI_SRT && (cfg->srt_passphrase_in[0] || cfg->srt_pbkeylen_in || cfg->srt_streamid_in[0] || cfg->srt_packetfilter_in[0] || cfg->srt_latency_in_ms))
    log_line(TOOL_NAME ": --srt-*-in needs -i srt:// source");
  if (rec_validate_srt_passphrase(cfg->srt_passphrase, cfg->srt_pbkeylen, "")) return ARGS_ERR;

  has_srt_out = 0;
  for (int i = 0; i < cfg->n_out; i++) {
    if (cfg->out[i].kind == OUT_SRT) has_srt_out = 1;
  }
  if (!has_srt_out && (cfg->srt_passphrase[0] || cfg->srt_pbkeylen || cfg->srt_streamid[0] || cfg->srt_packetfilter[0] || cfg->srt_latency_ms))
    log_line(TOOL_NAME ": --srt-* needs -o srt:// target");
  return ARGS_OK;
}
