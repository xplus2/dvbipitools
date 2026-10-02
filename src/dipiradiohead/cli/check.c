/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/cas/cas_args.h"
#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"

#include "priv.h"

static int is_sid_used(const unsigned *used, unsigned n_used, unsigned sid) {
  for (unsigned j = 0; j < n_used; j++) if (used[j] == sid) return 1;
  return 0;
}

/* assigns the smallest unused positive sid to inputs that didn't get an explicit --sid.
   -1: duplicate --sid given explicitly, 0 ok */
static int assign_missing_sids(config_t *cfg) {
  unsigned used[RADIOHEAD_MAX_INPUTS];
  unsigned n_used = 0;
  unsigned next = 1;
  for (unsigned i = 0; i < cfg->n_inputs; i++) {
    if (cfg->inputs[i].sid == 0) continue;
    if (is_sid_used(used, n_used, cfg->inputs[i].sid)) {
      argerr("duplicate --sid %u", cfg->inputs[i].sid);
      return -1;
    }
    used[n_used++] = cfg->inputs[i].sid;
  }
  for (unsigned i = 0; i < cfg->n_inputs; i++) {
    if (cfg->inputs[i].sid != 0) continue;
    while (is_sid_used(used, n_used, next)) next++;
    cfg->inputs[i].sid = next;
    used[n_used++] = next;
    next++;
  }
  return 0;
}

args_status_t rdh_cli_check(config_t *cfg, const rdh_opt_t *p) {
  int have_mcast = p->have_mcast;
  int any_cas_flag = p->any_cas_flag;
  int have_secret = p->have_secret;
  const char *profile_arg = p->profile_arg;
  const char *srt_group_mode_arg = p->srt_group_mode_arg;

  have_mcast = have_mcast || cfg->mcast_port;
  any_cas_flag = any_cas_flag || cfg->any_cas_flag;
  have_secret = have_secret || cfg->rist_secret[0];
  if (cfg->n_inputs == 0) {
    argerr("missing -i input");
    return ARGS_ERR;
  }
  if (!have_mcast && cfg->n_rist == 0 && cfg->n_srt == 0) {
    argerr("need -m output multicast or at least one -R peer");
    return ARGS_ERR;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) return ARGS_ERR;
  if (argutil_metrics_inspect_ts_validate(TOOL_NAME, cfg->metrics_id, cfg->metrics_inspect_ts)) return ARGS_ERR;
  if (cfg->metrics_n_known_pids && cfg->metrics_inspect_ts != METRICS_INSPECT_TS_FULL) {
    argutil_err(TOOL_NAME, "--metrics-inspect-ts-pids requires --metrics-inspect-ts full");
    return ARGS_ERR;
  }
  if (cfg->al_fec_l && !cfg->al_fec_port) {
    argerr("--al-fec requires --al-fec-port");
    return ARGS_ERR;
  }
  if (!cfg->al_fec_l && cfg->al_fec_port)
    log_line(TOOL_NAME ": --al-fec-port has no effect without --al-fec");
  if (cfg->al_fec_l && !cfg->rtp) {
    argerr("--al-fec requires -r/--rtp output");
    return ARGS_ERR;
  }
  if (cfg->al_fec_l && !cfg->mcast_port)
    log_line(TOOL_NAME ": --al-fec has no effect without -m");
  if (profile_arg) {
    static const enum_map_t map[] = {{"simple", RIST_PROF_SIMPLE}, {"main", RIST_PROF_MAIN}};
    int v;
    if (map_lookup(map, sizeof map / sizeof map[0], profile_arg, &v)) {
      argerr("invalid --rist-profile: %s (simple|main)", profile_arg);
      return ARGS_ERR;
    }
    cfg->rist_profile = (rist_profile_sel_t)v;
  }
  if (cfg->n_rist == 0 && (profile_arg || cfg->rist_profile_given || have_secret || cfg->rist_key_size || cfg->rist_cname[0] || cfg->rist_buffer_ms))
    log_line(TOOL_NAME ": --rist-profile/--rist-secret/--rist-encryption-type/--rist-cname/--rist-buffer have no effect without -R");
  if (cfg->n_rist > 0 && have_secret && cfg->rist_profile != RIST_PROF_MAIN) {
    argerr("--rist-secret requires --rist-profile main");
    return ARGS_ERR;
  }
  if (cfg->n_rist > 0 && cfg->rist_key_size && cfg->rist_profile != RIST_PROF_MAIN) {
    argerr("--rist-encryption-type requires --rist-profile main");
    return ARGS_ERR;
  }
  if (srt_group_mode_arg) {
    static const enum_map_t map[] = {{"broadcast", SRT_BOND_BROADCAST}, {"backup", SRT_BOND_BACKUP}};
    int v;
    if (map_lookup(map, sizeof map / sizeof map[0], srt_group_mode_arg, &v)) {
      argerr("invalid --srt-group-mode: %s (broadcast|backup)", srt_group_mode_arg);
      return ARGS_ERR;
    }
    cfg->srt_group_mode = (srt_bond_mode_t)v;
  }
  if (cfg->n_srt > 1 && cfg->srt_group_mode == SRT_BOND_NONE) {
    argerr("bonding several -R srt:// peers requires --srt-group-mode");
    return ARGS_ERR;
  }
  if (cfg->n_srt == 1 && cfg->srt_group_mode != SRT_BOND_NONE) {
    argerr("--srt-group-mode has no effect with a single -R srt:// peer");
    return ARGS_ERR;
  }
  if (cfg->n_srt == 0 && (srt_group_mode_arg || cfg->srt_group_mode != SRT_BOND_NONE || cfg->srt_passphrase[0] || cfg->srt_pbkeylen || cfg->srt_streamid[0] || cfg->srt_packetfilter[0] || cfg->srt_latency_ms))
    log_line(TOOL_NAME ": --srt-* has no effect without an -R srt:// peer");
  if (cfg->srt_passphrase[0] && (strlen(cfg->srt_passphrase) < 10 || strlen(cfg->srt_passphrase) > 79)) {
    argerr("--srt-passphrase must be 10..79 characters");
    return ARGS_ERR;
  }
  if (cfg->srt_pbkeylen && !cfg->srt_passphrase[0]) {
    argerr("--srt-pbkeylen requires --srt-passphrase");
    return ARGS_ERR;
  }
  if (any_cas_flag && cfg->cas_algo == CAS_ALGO_NONE) {
    argerr("--cas-* options require --cas-algo");
    return ARGS_ERR;
  }
  if (cas_args_validate(TOOL_NAME, &(cas_args_t){cfg->cas_algo, cfg->cas_vendors, cfg->n_cas_vendors, cfg->biss2_enabled, cfg->biss1_enabled,
    cfg->biss2_ca_enabled, cfg->biss2_emit_esw, cfg->biss2_ca_session_id_given, cfg->cas_cp_duration_ms}) != 0)
    return ARGS_ERR;
  if (assign_missing_sids(cfg) != 0) return ARGS_ERR;
  for (unsigned i = 0; i < cfg->n_inputs; i++) {
    if (cfg->inputs[i].sdt_text[0]) continue;
    if (cfg->n_inputs == 1) bufcpy(cfg->inputs[i].sdt_text, sizeof cfg->inputs[i].sdt_text, TOOL_NAME);
    else {
      sbuf_t nb;
      sbuf_init(&nb, cfg->inputs[i].sdt_text, sizeof cfg->inputs[i].sdt_text);
      sbuf_add(&nb, TOOL_NAME " ");
      sbuf_add_uint(&nb, i + 1);
    }
  }
  return ARGS_OK;
}
