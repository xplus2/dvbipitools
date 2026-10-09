/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  for (int k = 0; k < count; k++) {
    if (es[k].ca_pid) return &es[k];
  }
  return NULL;
}

static int build_es_map(const remux_t *r, const psi_t *psi, out_es_t *es, unsigned *pcr_out, int *dropped) {
  int count;
  const psi_es_t *in_es = psi_es(psi, &count);
  int n = pmtbuild_map_es(in_es, count, r->input.strip_mask, psi_pcr_pid(psi), r->pids.video_pid, r->pids.es_pid_base, es, OUT_PROGRAM_ES_CAP, pcr_out, dropped);
  int own_cas = r->cfg.cas_algo != CAS_ALGO_NONE || r->cfg.biss1_enabled || r->cfg.biss2_enabled || r->cfg.biss2_ca_enabled;
  unsigned ecm_pid = 0;
  unsigned ecm_sysid = 0;
  unsigned emm_pid = 0;
  unsigned emm_sysid = 0;

  if (n <= 0) return n;
  if (!own_cas && !(r->input.strip_mask & TVSTRIP_ECM)) {
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
    emm_pid = r->emm_pid_in;
    emm_sysid = r->emm_sysid_in;
  }
  pmtbuild_add_ca_passthrough(ecm_pid, ecm_sysid, emm_pid, emm_sysid, r->pids.es_pid_base, r->pids.video_pid, es, &n, OUT_PROGRAM_ES_CAP, dropped);
  return n;
}

remux_t *remux_new(const config_t *cfg, const dipitvhead_input_t *input, const psi_t *psi, const out_program_pids_t *pids, int standalone) {
  remux_t *r = calloc(1, sizeof *r);
  int n;
  int dropped;
  const unsigned char *pmt_sec;

  if (!r) return NULL;
  r->cfg = *cfg;
  r->input = *input;
  r->pids = *pids;
  r->standalone = standalone;
  r->src_service_id = psi_program_number(psi);
  r->pcr_pid_in = psi_pcr_pid(psi);
  r->emm_pid_in = psi_emm_pid(psi);
  r->emm_sysid_in = psi_ca_system_id(psi);
  n = build_es_map(r, psi, r->es, &r->pcr_pid_out, &dropped);
  if (n <= 0) {
    free(r);
    return NULL;
  }
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
  pmt_sec = psi_pmt_section(psi, &r->src_pmt_len);
  if (pmt_sec && r->src_pmt_len <= sizeof r->src_pmt) {
    memcpy(r->src_pmt, pmt_sec, r->src_pmt_len);
    r->watch = psi_new();
    if (r->watch) {
      char label[32];
      snprintf(label, sizeof label, "program %u watch", r->src_service_id);
      psi_set_label(r->watch, label);
      r->watch_pmt_pid = psi_pmt_pid(psi);
      psi_select_pmt_pid(r->watch, r->watch_pmt_pid);
    }
  } else {
    r->src_pmt_len = 0;
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
  psi_free(r->watch);
  free(r);
}

static void pick_hold_ref(remux_t *r) {
  r->hold_ref = -1;
  for (int i = 0; i < r->es_count; i++) {
    if (r->es[i].out_pid == r->pids.video_pid) r->hold_ref = i;
  }
  for (int i = 0; i < r->es_count && r->hold_ref < 0; i++) {
    if (r->es[i].is_ca == CA_PASS_NONE) r->hold_ref = i;
  }
}

int remux_set_hold(remux_t *r, remux_clock_fn clock, remux_latch_fn latch, void *clock_ctx, unsigned lead_ms) {
  for (int i = 0; i < r->es_count; i++) {
    if (r->hold[i]) continue;
    r->hold[i] = releaseq_new(HOLD_MAX_PACKETS);
    if (!r->hold[i]) return -1;
  }
  pick_hold_ref(r);
  r->hold_clock = clock;
  r->hold_latch = latch;
  r->hold_clock_ctx = clock_ctx;
  r->hold_lead_cfg90 = (uint64_t)lead_ms * 90;
  r->hold_lead90 = r->hold_lead_cfg90;
  return 0;
}

static int find_old_es(const remux_t *r, const out_es_t *e) {
  for (int j = 0; j < r->es_count; j++) {
    if (r->es[j].in_pid == e->in_pid && r->es[j].is_ca == e->is_ca) return j;
  }
  return -1;
}

static unsigned pick_pcr_out(const out_es_t *es, int n, unsigned src_pcr_pid) {
  for (int i = 0; i < n; i++) {
    if (es[i].in_pid == src_pcr_pid) return es[i].out_pid;
  }
  return es[0].out_pid;
}

static void keep_out_pids(const remux_t *r, out_es_t *neu, int n) {
  unsigned char used[OUT_PROGRAM_ES_CAP] = {0};
  unsigned char kept[OUT_PROGRAM_ES_CAP] = {0};
  for (int i = 0; i < n; i++) {
    int j = find_old_es(r, &neu[i]);
    if (j < 0) continue;
    neu[i].out_pid = r->es[j].out_pid;
    if (neu[i].out_pid < r->pids.video_pid || neu[i].out_pid - r->pids.video_pid >= OUT_PROGRAM_ES_CAP) continue;
    used[neu[i].out_pid - r->pids.video_pid] = 1;
    kept[i] = 1;
  }
  for (int i = 0; i < n; i++) {
    unsigned off = neu[i].out_pid - r->pids.video_pid;
    if (kept[i]) continue;
    if (neu[i].out_pid < r->pids.video_pid || off >= OUT_PROGRAM_ES_CAP || used[off]) {
      for (off = 0; off < OUT_PROGRAM_ES_CAP && used[off]; off++) {
      }
      neu[i].out_pid = r->pids.video_pid + off;
    }
    if (off < OUT_PROGRAM_ES_CAP) used[off] = 1;
  }
}

static int want_scte(const remux_t *r, const out_es_t *e) {
  return r->cfg.pcr_mode != PCR_MODE_PRESERVE && e->stream_type == 0x86 && e->is_ca == CA_PASS_NONE;
}

static void migrate_slots(remux_t *r, const out_es_t *neu, int n) {
  releaseq_t *hold[OUT_PROGRAM_ES_CAP] = {0};
  scte35stamp_t *scte[OUT_PROGRAM_ES_CAP] = {0};
  uint64_t tag[OUT_PROGRAM_ES_CAP] = {0};
  unsigned char has_tag[OUT_PROGRAM_ES_CAP] = {0};
  unsigned char moved[OUT_PROGRAM_ES_CAP] = {0};

  for (int i = 0; i < n; i++) {
    int j = find_old_es(r, &neu[i]);
    if (j >= 0) {
      hold[i] = r->hold[j];
      scte[i] = r->scte[j];
      tag[i] = r->hold_tag[j];
      has_tag[i] = r->hold_has_tag[j];
      moved[j] = 1;
    }
    if (!hold[i] && r->hold_clock) hold[i] = releaseq_new(HOLD_MAX_PACKETS);
    if (!want_scte(r, &neu[i])) {
      free(scte[i]);
      scte[i] = NULL;
    } else if (!scte[i]) {
      scte[i] = malloc(sizeof *scte[i]);
      if (scte[i]) scte35stamp_init(scte[i]);
    }
  }
  for (int j = 0; j < r->es_count; j++) {
    if (moved[j]) continue;
    releaseq_free(r->hold[j]);
    free(r->scte[j]);
  }
  memcpy(r->hold, hold, sizeof hold);
  memcpy(r->scte, scte, sizeof scte);
  memcpy(r->hold_tag, tag, sizeof tag);
  memcpy(r->hold_has_tag, has_tag, sizeof has_tag);
}

static void cas_add_new_streams(remux_t *r, const out_es_t *neu, int n) {
  out_es_t added[OUT_PROGRAM_ES_CAP];
  unsigned pids[CAS_CORE_MAX_PIDS];
  int na = 0;
  size_t np;
  size_t failed;
  if (!r->cas) return;
  for (int i = 0; i < n; i++) {
    if (find_old_es(r, &neu[i]) < 0) added[na++] = neu[i];
  }
  if (!na) return;
  np = cas_resolve_pids(&r->cfg, added, na, pids, CAS_CORE_MAX_PIDS);
  failed = np ? cas_add_pids(r->cas, pids, np) : 0;
  if (failed) log_line("program %u: cas pid set full, %zu new stream%s go out unscrambled", r->src_service_id, failed, failed == 1 ? "" : "s");
}

static void remap_es(remux_t *r, double now, remux_packet_cb cb, void *ctx, ts_metrics_t *tsm) {
  out_es_t neu[OUT_PROGRAM_ES_CAP];
  unsigned pcr_out;
  int dropped;
  int n = build_es_map(r, r->watch, neu, &pcr_out, &dropped);

  if (n <= 0) return;
  keep_out_pids(r, neu, n);
  if (pick_pcr_out(neu, n, psi_pcr_pid(r->watch)) != r->pcr_pid_out) {
    r->reconnect_wanted = 1;
    return;
  }
  if (dropped) log_line("program %u: ES cap (%d) reached, dropping %d stream%s", r->src_service_id, OUT_PROGRAM_ES_CAP, dropped, dropped == 1 ? "" : "s");
  remux_release(r, now, cb, ctx, 0, tsm);
  for (int j = 0; j < r->es_count; j++) {
    int kept = 0;
    for (int i = 0; i < n && !kept; i++) kept = find_old_es(r, &neu[i]) == j;
    if (!kept) flush_hold_slot(r, j, now, cb, ctx);
  }
  cas_add_new_streams(r, neu, n);
  migrate_slots(r, neu, n);
  memcpy(r->es, neu, sizeof neu[0] * (size_t)n);
  r->es_count = n;
  r->last_es_idx = 0;
  r->pcr_pid_in = psi_pcr_pid(r->watch);
  if (r->hold_clock) pick_hold_ref(r);
}

static int follow_pmt_pid(remux_t *r) {
  int n;
  const psi_program_t *p = psi_pat_programs(r->watch, &n);
  psi_t *w;
  unsigned pid;
  char label[32];

  for (int i = 0; i < n; i++) {
    if (p[i].program_number != r->src_service_id) continue;
    pid = p[i].pmt_pid;
    if (pid == r->watch_pmt_pid) return 0;
    w = psi_new();
    if (!w) return 0;
    log_line("program %u: PMT pid 0x%x -> 0x%x", r->src_service_id, r->watch_pmt_pid, pid);
    snprintf(label, sizeof label, "program %u watch", r->src_service_id);
    psi_set_label(w, label);
    psi_free(r->watch);
    r->watch = w;
    r->watch_pmt_pid = pid;
    r->watch_parsed = 0;
    psi_select_pmt_pid(w, r->watch_pmt_pid);
    return 1;
  }
  return 0;
}

void watch_source_pmt(remux_t *r, double now, const unsigned char *pkt188, remux_packet_cb cb, void *ctx, ts_metrics_t *tsm) {
  const unsigned char *sec;
  size_t len;

  psi_feed(r->watch, pkt188);
  if (follow_pmt_pid(r)) return;
  if (psi_pmt_parsed(r->watch) == r->watch_parsed) return;
  r->watch_parsed = psi_pmt_parsed(r->watch);
  sec = psi_pmt_section(r->watch, &len);
  if (!sec || len > sizeof r->src_pmt || (len == r->src_pmt_len && !memcmp(sec, r->src_pmt, len))) return;
  memcpy(r->src_pmt, sec, len);
  r->src_pmt_len = len;
  remap_es(r, now, cb, ctx, tsm);
}

unsigned remux_pcr_pid_out(const remux_t *r) { return r->pcr_pid_out; }

int remux_reconnect_wanted(const remux_t *r) { return r->reconnect_wanted; }

void remux_set_psi_versions(remux_t *r, psi_versions_t *pv) { r->pv = pv; }

void remux_set_cc_offsets(remux_t *r, cc_offsets_t *m) {
  r->ccm = m;
  if (!m) return;
  for (int i = 0; i < OUT_PROGRAM_ES_CAP; i++)
    if (m->last[i] & 0x10) m->last[i] |= 0x20;
}

const out_es_t *remux_es(const remux_t *r, int *count) {
  *count = r->es_count;
  return r->es;
}

const out_es_t *find_ca_passthrough(const remux_t *r, ca_pass_t is_ca) {
  for (int i = 0; i < r->es_count; i++) {
    if (r->es[i].is_ca == is_ca) return &r->es[i];
  }
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
