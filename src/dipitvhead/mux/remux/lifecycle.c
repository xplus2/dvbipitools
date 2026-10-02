/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/mux/cadescbuild.h"

#include "../../version.h"
#include "priv.h"

static void resolve_sdt(remux_t *r, const psi_t *psi) {
  if (r->input.sdt_mode == TABLE_DROP) {
    r->send_sdt = 0;
  } else if (r->input.sdt_mode == TABLE_OVERRIDE) {
    const char *prov = r->input.provider_text;
    bufcpy(r->service_name, sizeof r->service_name, r->input.sdt_text);
    if (!prov[0]) prov = r->cfg.default_provider_text;
    if (!prov[0]) prov = TOOL_NAME;
    bufcpy(r->provider_name, sizeof r->provider_name, prov);
    r->send_sdt = 1;
  } else {
    bufcpy(r->service_name, sizeof r->service_name, psi_service_name(psi));
    bufcpy(r->provider_name, sizeof r->provider_name, r->input.provider_text[0] ? r->input.provider_text : psi_provider_name(psi));
    r->send_sdt = r->service_name[0] != '\0';
  }
}

static void resolve_nit(remux_t *r, const psi_t *psi) {
  if (r->cfg.nit_mode == TABLE_DROP) {
    r->send_nit = 0;
  } else if (r->cfg.nit_mode == TABLE_OVERRIDE) {
    bufcpy(r->network_name, sizeof r->network_name, r->cfg.nit_text);
    r->send_nit = 1;
  } else {
    bufcpy(r->network_name, sizeof r->network_name, psi_network_name(psi));
    r->send_nit = r->network_name[0] != '\0';
  }
}

static const psi_es_t *find_first_ca_es(const psi_es_t *es, int count) {
  for (int k = 0; k < count; k++) if (es[k].ca_pid) return &es[k];
  return NULL;
}

remux_t *remux_new(const config_t *cfg, const dipitvhead_input_t *input, const psi_t *psi, const out_program_pids_t *pids, int standalone) {
  remux_t *r = calloc(1, sizeof *r);
  int n;
  int count;
  int dropped;
  const psi_es_t *in_es;
  /* exclusive with own CAS/BISS: both would want OUT_PID_CAT */
  int own_cas;
  unsigned ecm_pid;
  unsigned ecm_sysid;
  unsigned emm_pid;
  unsigned emm_sysid;

  if (!r) return NULL;
  r->cfg = *cfg;
  r->input = *input;
  r->pids = *pids;
  r->standalone = standalone;
  r->src_service_id = psi_program_number(psi);
  r->pcr_pid_in = psi_pcr_pid(psi);
  in_es = psi_es(psi, &count);
  n = pmtbuild_map_es(in_es, count, input->strip_mask, psi_pcr_pid(psi), r->pids.video_pid, r->pids.es_pid_base, r->es, OUT_PROGRAM_ES_CAP, &r->pcr_pid_out, &dropped);
  if (n <= 0) {
    free(r);
    return NULL;
  }

  own_cas = cfg->cas_algo != CAS_ALGO_NONE || cfg->biss1_enabled || cfg->biss2_enabled || cfg->biss2_ca_enabled;
  ecm_pid = 0;
  ecm_sysid = 0;
  emm_pid = 0;
  emm_sysid = 0;
  if (!own_cas && !(input->strip_mask & TVSTRIP_ECM)) {
    if (psi_pmt_ca_pid(psi)) {
      ecm_pid = psi_pmt_ca_pid(psi);
      ecm_sysid = psi_pmt_ca_system_id(psi);
    } else {
      const psi_es_t *ca_es = find_first_ca_es(in_es, count);
      if (ca_es) {
        ecm_pid = ca_es->ca_pid;
        ecm_sysid = ca_es->ca_system_id;
      }
    }
    if (psi_emm_pid(psi)) {
      emm_pid = psi_emm_pid(psi);
      emm_sysid = psi_ca_system_id(psi);
    }
  }
  pmtbuild_add_ca_passthrough(ecm_pid, ecm_sysid, emm_pid, emm_sysid, r->pids.es_pid_base, r->pids.video_pid, r->es, &n, OUT_PROGRAM_ES_CAP, &dropped);
  r->es_count = n;
  if (cfg->pcr_mode != PCR_MODE_PRESERVE) {
    for (int i = 0; i < n; i++) {
      if (r->es[i].stream_type != 0x86 || r->es[i].is_ca != CA_PASS_NONE) continue;
      r->scte[i] = malloc(sizeof *r->scte[i]);
      if (r->scte[i]) scte35stamp_init(r->scte[i]);
    }
  }
  if (dropped) log_line("program %u: ES cap (%d) reached, dropping %d stream%s", r->src_service_id, OUT_PROGRAM_ES_CAP, dropped, dropped == 1 ? "" : "s");
  resolve_sdt(r, psi);
  resolve_nit(r, psi);
  r->send_ait = r->input.hbbtv_url != NULL;
  if (r->send_ait) {
    r->ait_pmt_entry_len = aitbuild_pmt_entry(r->pids.ait_pid, r->ait_pmt_entry, sizeof r->ait_pmt_entry);
    r->ait_section_len = aitbuild_ait(0, r->input.hbbtv_org_id, r->input.hbbtv_app_id, r->input.hbbtv_url, r->ait_section, sizeof r->ait_section);
    r->send_ait = r->ait_pmt_entry_len && r->ait_section_len;
    if (!r->send_ait) log_line("--hbbtv: AIT build failed (url too long?)");
  }
  r->last_pat = -1.0;
  r->last_sdt = -1.0;
  r->last_nit = -1.0;
  r->last_ait = -1.0;
  r->last_cat = -1.0;
  return r;
}

#define HOLD_MAX_PACKETS 32768

void remux_free(remux_t *r) {
  if (!r) return;
  for (int i = 0; i < OUT_PROGRAM_ES_CAP; i++) {
    releaseq_free(r->hold[i]);
    free(r->scte[i]);
  }
  free(r);
}

int remux_set_hold(remux_t *r, remux_clock_fn clock, remux_latch_fn latch, void *clock_ctx, unsigned lead_ms) {
  r->hold_ref = -1;
  for (int i = 0; i < r->es_count; i++) {
    if (r->hold[i]) continue;
    r->hold[i] = releaseq_new(HOLD_MAX_PACKETS);
    if (!r->hold[i]) return -1;
  }
  for (int i = 0; i < r->es_count; i++) {
    if (r->es[i].out_pid == r->pids.video_pid) r->hold_ref = i;
  }
  for (int i = 0; i < r->es_count && r->hold_ref < 0; i++) {
    if (r->es[i].is_ca == CA_PASS_NONE) r->hold_ref = i;
  }
  r->hold_clock = clock;
  r->hold_latch = latch;
  r->hold_clock_ctx = clock_ctx;
  r->hold_lead_cfg90 = (uint64_t)lead_ms * 90;
  r->hold_lead90 = r->hold_lead_cfg90;
  return 0;
}
unsigned remux_pcr_pid_out(const remux_t *r) { return r->pcr_pid_out; }

const out_es_t *remux_es(const remux_t *r, int *count) {
  *count = r->es_count;
  return r->es;
}

const out_es_t *find_ca_passthrough(const remux_t *r, ca_pass_t is_ca) {
  for (int i = 0; i < r->es_count; i++) if (r->es[i].is_ca == is_ca) return &r->es[i];
  return NULL;
}

size_t remux_source_ca_descriptor(const remux_t *r, unsigned char *out, size_t cap) {
  const out_es_t *e = find_ca_passthrough(r, CA_PASS_ECM);
  if (!e) return 0;
  return cadescbuild_ca_descriptor(e->ca_system_id, e->out_pid, out, cap);
}

size_t remux_source_emm_descriptor(const remux_t *r, unsigned char *out, size_t cap) {
  const out_es_t *e = find_ca_passthrough(r, CA_PASS_EMM);
  if (!e) return 0;
  return cadescbuild_ca_descriptor(e->ca_system_id, e->out_pid, out, cap);
}

void remux_set_cas(remux_t *r, cas_t *cas) { r->cas = cas; }

void remux_set_timemap(remux_t *r, timemap_t *tm) { r->tm = tm; }

int remux_get_sdt_info(const remux_t *r, psi_sdt_entry_t *out) {
  if (!r->send_sdt) return -1;
  out->service_id = r->input.sid;
  out->service_type = 0x01;
  out->provider = r->provider_name;
  out->service_name = r->service_name;
  return 0;
}
