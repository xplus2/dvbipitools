/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/mux/cadescbuild.h"

#include "../../version.h"
#include "priv.h"

static void resolve_sdt(remux_t *r, const psi_t *psi) {
  if (r->input.sdt_mode == TABLE_DROP) {
    r->send_sdt = 0;
  } else if (r->input.sdt_mode == TABLE_OVERRIDE) {
    bufcpy(r->service_name, sizeof r->service_name, r->input.sdt_text);
    bufcpy(r->provider_name, sizeof r->provider_name, TOOL_NAME);
    r->send_sdt = 1;
  } else {
    bufcpy(r->service_name, sizeof r->service_name, psi_service_name(psi));
    bufcpy(r->provider_name, sizeof r->provider_name, psi_provider_name(psi));
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
  for (int k = 0; k < count; k++)
    if (es[k].ca_pid)
      return &es[k];
  return NULL;
}

remux_t *remux_new(const config_t *cfg, const dipitvhead_input_t *input, const psi_t *psi, const out_program_pids_t *pids, int standalone) {
  remux_t *r = calloc(1, sizeof *r);
  int n;
  int count;
  int dropped;
  const psi_es_t *in_es;

  if (!r)
    return NULL;
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
  {
    /* exclusive with own CAS/BISS: both would want OUT_PID_CAT */
    int own_cas = cfg->cas_algo != CAS_ALGO_NONE || cfg->biss1_enabled || cfg->biss2_enabled || cfg->biss2_ca_enabled;
    unsigned ecm_pid = 0;
    unsigned ecm_sysid = 0;
    unsigned emm_pid = 0;
    unsigned emm_sysid = 0;
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
  }
  r->es_count = n;
  if (dropped)
    log_line("program %u: ES cap (%d) reached, dropping %d stream%s", r->src_service_id, OUT_PROGRAM_ES_CAP, dropped, dropped == 1 ? "" : "s");
  resolve_sdt(r, psi);
  resolve_nit(r, psi);
  r->send_ait = r->input.hbbtv_url != NULL;
  if (r->send_ait) {
    r->ait_pmt_entry_len = aitbuild_pmt_entry(0, r->pids.ait_pid, r->ait_pmt_entry, sizeof r->ait_pmt_entry);
    r->ait_section_len = aitbuild_ait(0, r->input.hbbtv_org_id, r->input.hbbtv_app_id, r->input.hbbtv_url, r->ait_section, sizeof r->ait_section);
    r->send_ait = r->ait_pmt_entry_len && r->ait_section_len;
    if (!r->send_ait)
      log_line("--hbbtv: AIT build failed (url too long?), not sending it");
  }
  r->last_pat = -1.0;
  r->last_sdt = -1.0;
  r->last_nit = -1.0;
  r->last_ait = -1.0;
  r->last_cat = -1.0;
  return r;
}

void remux_free(remux_t *r) { free(r); }

unsigned remux_pcr_pid_out(const remux_t *r) { return r->pcr_pid_out; }

const out_es_t *remux_es(const remux_t *r, int *count) {
  *count = r->es_count;
  return r->es;
}

const out_es_t *find_ca_passthrough(const remux_t *r, int is_ca) {
  for (int i = 0; i < r->es_count; i++)
    if (r->es[i].is_ca == is_ca)
      return &r->es[i];
  return NULL;
}

size_t remux_source_ca_descriptor(const remux_t *r, unsigned char *out, size_t cap) {
  const out_es_t *e = find_ca_passthrough(r, 1);
  if (!e)
    return 0;
  return cadescbuild_ca_descriptor(e->ca_system_id, e->out_pid, out, cap);
}

size_t remux_source_emm_descriptor(const remux_t *r, unsigned char *out, size_t cap) {
  const out_es_t *e = find_ca_passthrough(r, 2);
  if (!e)
    return 0;
  return cadescbuild_ca_descriptor(e->ca_system_id, e->out_pid, out, cap);
}

void remux_set_cas(remux_t *r, cas_t *cas) { r->cas = cas; }

int remux_get_sdt_info(const remux_t *r, psi_sdt_entry_t *out) {
  if (!r->send_sdt)
    return -1;
  out->service_id = r->input.sid;
  out->service_type = 0x01;
  out->provider = r->provider_name;
  out->service_name = r->service_name;
  return 0;
}
