/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "lib/cas/cas_args.h"
#include "lib/helper/log.h"
#include "lib/scrambler/cissa.h"

#include "priv.h"
#include "../mux/pmtbuild.h"

static int sid_used(const unsigned *used, unsigned n_used, unsigned sid) {
  for (unsigned j = 0; j < n_used; j++) {
    if (used[j] == sid) return 1;
  }
  return 0;
}

static int has_duplicate_sid(const config_t *cfg) {
  unsigned used[ARGS_MAX_INPUTS] = {0};
  unsigned n_used = 0;
  for (unsigned i = 0; i < cfg->n_inputs; i++) {
    if (cfg->inputs[i].sid == 0) continue;
    if (sid_used(used, n_used, cfg->inputs[i].sid)) return 1;
    used[n_used++] = cfg->inputs[i].sid;
  }
  return 0;
}

/* ecm/emm pid vs fixed SI PIDs, per-program PMT pids + per-program ES blocks */
static int cas_pid_collides(unsigned pid, unsigned n_programs) {
  if (pid == OUT_PID_PAT || pid == OUT_PID_CAT || pid == OUT_PID_NIT || pid == OUT_PID_SDT || pid == OUT_PID_EIT) return 1;
  if (pid >= OUT_PID_PMT_BASE && pid < OUT_PID_PMT_BASE + n_programs) return 1;
  return pid >= OUT_PROGRAM_BLOCK_BASE && pid < OUT_PROGRAM_BLOCK_BASE + n_programs * OUT_MAX_ES;
}

int tvh_cfg_check(const config_t *cfg, int partial, tvh_report_fn rep, void *ud) {
  int fatal = 0;
  unsigned n_rist_in = 0;
  size_t pwlen = strlen(cfg->srt_passphrase);
  char msg[192];
#define FATAL(code, ...) do { snprintf(msg, sizeof msg, __VA_ARGS__); rep(ud, 1, code, msg); fatal++; } while (0)
#define NOTE(code, ...) do { snprintf(msg, sizeof msg, __VA_ARGS__); rep(ud, 0, code, msg); } while (0)

  if (!partial && cfg->n_inputs == 0) FATAL("TS000110", "missing -i input");
  for (unsigned i = 0; i < cfg->n_inputs; i++) {
    if (cfg->inputs[i].input.kind == SRC_RIST) n_rist_in++;
  }
  if (n_rist_in > 1) FATAL("TS000585", "at most one -i rist:// input: librist isn't safe with more than one context per process");
  if (n_rist_in && cfg->n_rist) FATAL("TS000586", "-i rist:// and -R rist:// cannot combine: librist isn't safe with more than one context per process");
  if (!partial && !cfg->mcast_port && cfg->n_rist == 0 && cfg->n_srt == 0) FATAL("TS000465", "need -m output multicast or at least one -R peer");
  if ((cfg->stuff || cfg->burst_limit) && !cfg->bitrate_kbps) FATAL("TS000587", "-S/--stuff and -B/--burst-limit need -b/--bitrate");
  if (cfg->pcr_mode == PCR_MODE_REGENERATE && (!cfg->bitrate_kbps || !cfg->stuff)) FATAL("TS000588", "--pcr-mode regenerate needs -b/--bitrate and -S/--stuff");
  if (cfg->pcr_mode == PCR_MODE_REGENERATE && cfg->bitrate_kbps && cfg->stuff && !cfg->burst_limit)
    NOTE("TA000589", "--pcr-mode regenerate without -B/--burst-limit: output can run ahead of the PCR timeline");
  if (cfg->pcr_lead_ms_given && cfg->pcr_mode != PCR_MODE_REGENERATE) FATAL("TS000590", "--pcr-lead-ms needs --pcr-mode regenerate");
  if ((cfg->metrics_sock || cfg->metrics_interval_s) && !cfg->metrics_id) FATAL("TS000591", "--metrics/--metrics-interval require --metrics-id");
  if (cfg->metrics_inspect_ts != METRICS_INSPECT_TS_OFF && !cfg->metrics_id) FATAL("TS000592", "--metrics-inspect-ts requires --metrics-id");
  if (cfg->metrics_n_known_pids && cfg->metrics_inspect_ts != METRICS_INSPECT_TS_FULL) FATAL("TS000336", "--metrics-inspect-ts-pids requires --metrics-inspect-ts full");
  if (cfg->al_fec_l && !cfg->al_fec_port) FATAL("TS000145", "--al-fec requires --al-fec-port");
  if (!cfg->al_fec_l && cfg->al_fec_port) NOTE("TA000146", "--al-fec-port has no effect without --al-fec");
  if (cfg->al_fec_l && !cfg->rtp) FATAL("TS000593", "--al-fec requires RTP output, not -u/--udp");
  if (cfg->al_fec_l && !cfg->mcast_port) NOTE("TA000467", "--al-fec has no effect without -m");
  if (cfg->n_rist == 0 && (cfg->rist_profile_given || cfg->rist_secret[0] || cfg->rist_key_size || cfg->rist_cname[0] || cfg->rist_buffer_ms))
    NOTE("TA000468", "--rist-profile/--rist-secret/--rist-encryption-type/--rist-cname/--rist-buffer have no effect without -R");
  if (cfg->n_rist > 0 && cfg->rist_secret[0] && cfg->rist_profile != RIST_PROF_MAIN) FATAL("TS000469", "--rist-secret requires --rist-profile main");
  if (cfg->n_rist > 0 && cfg->rist_key_size && cfg->rist_profile != RIST_PROF_MAIN) FATAL("TS000339", "--rist-encryption-type requires --rist-profile main");
  if (cfg->n_srt > 1 && cfg->srt_group_mode == SRT_BOND_NONE) FATAL("TS000471", "bonding several -R srt:// peers requires --srt-group-mode");
  if (cfg->n_srt == 1 && cfg->srt_group_mode != SRT_BOND_NONE) FATAL("TS000472", "--srt-group-mode has no effect with a single -R srt:// peer");
  if (cfg->n_srt == 0 && (cfg->srt_group_mode != SRT_BOND_NONE || cfg->srt_passphrase[0] || cfg->srt_pbkeylen ||
                          cfg->srt_streamid[0] || cfg->srt_packetfilter[0] || cfg->srt_latency_ms))
    NOTE("TA000473", "--srt-* has no effect without an -R srt:// peer");
  if (pwlen && (pwlen < 10 || pwlen > 79)) FATAL("TS000474", "--srt-passphrase must be 10..79 characters");
  if (cfg->srt_pbkeylen && !pwlen) FATAL("TS000342", "--srt-pbkeylen requires --srt-passphrase");
  for (unsigned i = 0; i < cfg->n_inputs; i++) {
    const dipitvhead_input_t *in = &cfg->inputs[i];
    size_t inlen = strlen(in->srt_passphrase_in);
    if (inlen && (inlen < 10 || inlen > 79)) FATAL("TS000594", "--srt-passphrase-in must be 10..79 characters");
    if (in->srt_pbkeylen_in && !inlen) FATAL("TS000340", "--srt-pbkeylen-in requires --srt-passphrase-in");
    if (in->input.kind != SRC_SRT && (inlen || in->srt_pbkeylen_in || in->srt_streamid_in[0] || in->srt_packetfilter_in[0] || in->srt_latency_in_ms))
      NOTE("TA000595", "--srt-*-in has no effect, that -i isn't srt://");
    if ((in->rist_key_size_in || in->rist_secret_in[0] || in->rist_cname_in[0] || in->rist_buffer_ms_in) && in->input.kind != SRC_RIST)
      NOTE("TA000596", "--rist-*-in has no effect, that -i isn't rist://");
    if (in->rist_key_size_in && in->input.kind == SRC_RIST && !in->rist_profile_main) FATAL("TS000542", "--rist-encryption-type-in requires --rist-profile-in main");
    if (in->rist_secret_in[0] && in->input.kind == SRC_RIST && !in->rist_profile_main) FATAL("TS000725", "--rist-secret-in requires --rist-profile-in main");
    if (in->jitter_ms && in->input.kind != SRC_RTP && in->input.kind != SRC_UDP && in->input.kind != SRC_RIST && in->input.kind != SRC_SRT)
      NOTE("TA000597", "--jitter-ms has no effect, that -i isn't rtp/udp/rist/srt");
    if (in->al_fec_l_in && !in->al_fec_port_in) FATAL("TS000729", "--al-fec-in requires --al-fec-port-in");
    if (!in->al_fec_l_in && in->al_fec_port_in) NOTE("TA000731", "--al-fec-port-in has no effect without --al-fec-in");
    if (in->al_fec_l_in && in->input.kind != SRC_RTP) NOTE("TA000730", "--al-fec-in has no effect, that -i isn't rtp://");
    if (in->hbbtv_url && (!in->hbbtv_org_id || !in->hbbtv_app_id)) FATAL("TS000598", "--hbbtv requires --hbbtv-org-id and --hbbtv-app-id");
    if ((in->hbbtv_org_id || in->hbbtv_app_id || in->hbbtv_name[0]) && !in->hbbtv_url) FATAL("TS000599", "--hbbtv-org-id/--hbbtv-app-id/--hbbtv-name need --hbbtv");
  }
  if (cfg->any_cas_flag && cfg->cas_algo == CAS_ALGO_NONE && !cfg->biss2_enabled && !cfg->biss1_enabled && !cfg->biss2_ca_enabled)
    FATAL("TS000600", "--cas-* options require --cas-algo (or --cas-pids alone under --biss2-sw/--biss1-sw/--biss2-ca-receivers)");
  if ((cfg->cas_algo == CAS_ALGO_CISSA || cfg->biss2_enabled || cfg->biss2_ca_enabled) && !cissa_available())
    FATAL("TS000726", "CISSA/BISS-2 needs a build with OpenSSL");
  if (cas_args_validate(TOOL_NAME, &(cas_args_t){cfg->cas_algo, cfg->cas_vendors, cfg->n_cas_vendors, cfg->biss2_enabled, cfg->biss1_enabled,
    cfg->biss2_ca_enabled, cfg->biss2_emit_esw, cfg->biss2_ca_session_id_given, cfg->cas_cp_duration_ms}) != 0)
    fatal++;
  if (cfg->cas_algo != CAS_ALGO_NONE) {
    unsigned n_programs = cfg->n_inputs ? cfg->n_inputs : 1;
    for (unsigned vi = 0; vi < cfg->n_cas_vendors; vi++) {
      if (cas_pid_collides(cfg->cas_vendors[vi].ecm_pid, n_programs))
        FATAL("TS000718", "--cas-ecm-pid 0x%04X collides with a PID the muxer emits (PAT, CAT, NIT, SDT, EIT, PMT, program ES block)", cfg->cas_vendors[vi].ecm_pid);
      if (cas_pid_collides(cfg->cas_vendors[vi].emm_pid, n_programs))
        FATAL("TS000718", "--cas-emm-pid 0x%04X collides with a PID the muxer emits (PAT, CAT, NIT, SDT, EIT, PMT, program ES block)", cfg->cas_vendors[vi].emm_pid);
    }
  }
  if (has_duplicate_sid(cfg)) FATAL("TS000601", "duplicate --sid");
#undef FATAL
#undef NOTE
  return fatal;
}

void tvh_finalize(config_t *cfg) {
  unsigned used[ARGS_MAX_INPUTS];
  unsigned n_used = 0;
  unsigned next = 1;
  int have_cas_pids = cfg->cas_pid_count || cfg->cas_pids_video || cfg->cas_pids_audio || cfg->cas_pids_lcevc;

  if (!have_cas_pids && (cfg->cas_algo != CAS_ALGO_NONE || cfg->biss1_enabled || cfg->biss2_enabled || cfg->biss2_ca_enabled)) {
    /* default: scramble all video and audio elementary streams */
    cfg->cas_pids_video = 1;
    cfg->cas_pids_audio = 1;
  }
  if (cfg->cas_algo != CAS_ALGO_NONE || cfg->biss1_enabled || cfg->biss2_enabled || cfg->biss2_ca_enabled) {
    for (unsigned i = 0; i < cfg->n_inputs; i++) {
      if (!(cfg->inputs[i].strip_mask & TVSTRIP_ECM)) {
        log_code("TS300164", TOOL_NAME ": source CA/ECM passthrough disabled: --cas-algo/--biss* already scrambling this mux");
        break;
      }
    }
  }
  for (unsigned i = 0; i < cfg->n_inputs; i++) {
    if (cfg->inputs[i].sid) used[n_used++] = cfg->inputs[i].sid;
  }
  for (unsigned i = 0; i < cfg->n_inputs; i++) {
    if (cfg->inputs[i].sid != 0) continue;
    while (sid_used(used, n_used, next)) next++;
    cfg->inputs[i].sid = next;
    used[n_used++] = next;
    next++;
  }
}
