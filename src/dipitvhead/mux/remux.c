/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/psi/section_asm.h"
#include "lib/demux/tspack.h"
#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/mux/cadescbuild.h"
#include "lib/mux/psi_build.h"
#include "lib/mux/tspacket_write.h"

#include "../cas/cas.h"
#include "../version.h"

#include "aitbuild.h"
#include "pmtbuild.h"
#include "remux.h"

#define INTERVAL_PAT_PMT_S 0.1
#define INTERVAL_SDT_S 2.0
#define INTERVAL_NIT_S 10.0
#define INTERVAL_AIT_S 0.5

#define EIT_QUEUE_CAP 16 /* distinct (table_id, section_number) sections held at once */

typedef struct {
  unsigned char table_id, section_number;
  unsigned char data[4096];
  size_t len;
} eit_section_t;

struct remux {
  config_t cfg;
  dipitvhead_input_t input;
  out_program_pids_t pids;
  int standalone;
  unsigned src_service_id; /* source's own service_id, for filtering captured EIT sections */

  out_es_t es[OUT_PROGRAM_ES_CAP];
  int es_count;
  unsigned pcr_pid_out;
  cas_t *cas;

  int send_sdt, send_nit, send_ait;
  char service_name[256], provider_name[PSI_NAME], network_name[256];
  unsigned char ait_pmt_entry[16];
  size_t ait_pmt_entry_len;
  unsigned char ait_section[300];
  size_t ait_section_len;

  unsigned char last_pmt[4096];
  size_t last_pmt_len;
  int have_last_pmt;

  unsigned char cc_pat, cc_pmt, cc_sdt, cc_nit, cc_eit, cc_ait, cc_cat;
  unsigned char cc_ecm[ARGS_MAX_CAS_VENDORS], cc_emm[ARGS_MAX_CAS_VENDORS];
  unsigned char cc_es[OUT_PROGRAM_ES_CAP];
  int last_es_idx; /* MRU 1-entry cache: consecutive packets are usually the same pid */

  double last_pat, last_sdt, last_nit, last_ait, last_cat;

  /* TS-integrity tracking, source-side, per-pid; resets on reconnect (new remux_t) unlike
     ts_metrics_t's cumulative counts, which the caller owns across reconnects */
  unsigned char cc_state[8192]; /* bit 0x10 = seen; low nibble = last continuity_counter */
  unsigned pcr_pid_in;          /* source's own PCR pid (psi_pcr_pid at discovery); 0 = none */
  int have_last_pcr;
  uint64_t last_pcr27;
  double last_pcr_wall;

  /* non-standalone only: reassembles source EIT (filtered to src_service_id) into a drainable
     queue - see remux_emit_eit(). eit_drain_off: byte offset into eit_queue[0] mid-emit. */
  psi_section_asm_t eit_asm;
  eit_section_t eit_queue[EIT_QUEUE_CAP];
  int eit_queue_count;
  size_t eit_drain_off;
};

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

remux_t *remux_new(const config_t *cfg, const dipitvhead_input_t *input, const psi_t *psi, const out_program_pids_t *pids, int standalone) {
  remux_t *r = calloc(1, sizeof *r);
  int n, count, dropped;
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
    unsigned ecm_pid = 0, ecm_sysid = 0, emm_pid = 0, emm_sysid = 0;
    if (!own_cas && !(input->strip_mask & TVSTRIP_ECM)) {
      if (psi_pmt_ca_pid(psi)) {
        ecm_pid = psi_pmt_ca_pid(psi);
        ecm_sysid = psi_pmt_ca_system_id(psi);
      } else {
        for (int k = 0; k < count; k++)
          if (in_es[k].ca_pid) {
            ecm_pid = in_es[k].ca_pid;
            ecm_sysid = in_es[k].ca_system_id;
            break;
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
  r->last_pat = r->last_sdt = r->last_nit = r->last_ait = r->last_cat = -1.0;
  return r;
}

void remux_free(remux_t *r) { free(r); }

unsigned remux_pcr_pid_out(const remux_t *r) { return r->pcr_pid_out; }

const out_es_t *remux_es(const remux_t *r, int *count) {
  *count = r->es_count;
  return r->es;
}

/* is_ca 1 (ECM) or 2 (EMM) entry, NULL if this program carries none */
static const out_es_t *find_ca_passthrough(const remux_t *r, int is_ca) {
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

const char *psi_table_name(psi_table_t table) {
  static const char *const names[PSI_TABLE_COUNT] = {"PAT", "PMT", "CAT", "SDT", "NIT"};
  if ((unsigned)table >= PSI_TABLE_COUNT)
    return "PAT";
  return names[table];
}

static int due(double now, double *last, double interval) {
  if (*last < 0.0 || now - *last >= interval) {
    *last = now;
    return 1;
  }
  return 0;
}

/* es[] index for a source pid, or -1 if not carried (dropped) */
static int find_es(remux_t *r, unsigned in_pid) {
  if (r->last_es_idx >= 0 && r->last_es_idx < r->es_count && r->es[r->last_es_idx].in_pid == in_pid)
    return r->last_es_idx;

  for (int i = 0; i < r->es_count; i++)
    if (r->es[i].in_pid == in_pid) {
      r->last_es_idx = i;
      return i;
    }
  return -1;
}

static void psi_note(ts_metrics_t *tsm, psi_table_t table, size_t n) {
  if (!tsm)
    return;
  if (n)
    tsm->psi_sections_total[table]++;
  else
    tsm->psi_errors_total[table]++;
}

/* tracks PMT change metrics and caches sec[0..n) as the last-seen PMT for next diff.
   have_last_pmt false (fresh remux_t, nothing to compare against) -> never "changed" */
static void track_pmt_metrics(remux_t *r, ts_metrics_t *tsm, const unsigned char *sec, size_t n) {
  int changed = r->have_last_pmt && (n != r->last_pmt_len || memcmp(sec, r->last_pmt, n) != 0);
  if (changed)
    tsm->pmt_updates_total++;
  if (n <= sizeof r->last_pmt) {
    memcpy(r->last_pmt, sec, n);
    r->last_pmt_len = n;
    r->have_last_pmt = 1;
  }
}

static void send_psi_tables(remux_t *r, double now, remux_packet_cb cb, void *ctx, ts_metrics_t *tsm) {
  unsigned char sec[4096];
  unsigned char ptr0 = 0x00;
  size_t n;

  if (due(now, &r->last_pat, INTERVAL_PAT_PMT_S)) {
    unsigned char prog_desc[32];
    size_t prog_desc_len = r->cas ? cas_prog_desc(r->cas, prog_desc, sizeof prog_desc) : remux_source_ca_descriptor(r, prog_desc, sizeof prog_desc);
    if (r->standalone) {
      n = psi_build_pat(r->cfg.tsid, 0, r->input.sid, r->pids.pmt_pid, sec, sizeof sec);
      psi_note(tsm, PSI_TABLE_PAT, n);
      if (n)
        ts_packet_emit(OUT_PID_PAT, &r->cc_pat, &ptr0, sec, n, 0, 0, cb, ctx);
    }
    n = pmtbuild_pmt(0, r->input.sid, r->pcr_pid_out, prog_desc_len ? prog_desc : NULL, prog_desc_len, r->es, r->es_count, r->send_ait ? r->ait_pmt_entry : NULL, r->send_ait ? r->ait_pmt_entry_len : 0, sec, sizeof sec);
    psi_note(tsm, PSI_TABLE_PMT, n);
    if (n) {
      /* diffing itself is metrics-only work, skipped entirely when tsm is NULL - not just
         its counter write. "updated" is relative to this remux_t's own history - a fresh
         remux_t (reconnect) starts with no prior PMT, so its first build here never counts */
      if (tsm)
        track_pmt_metrics(r, tsm, sec, n);
      ts_packet_emit(r->pids.pmt_pid, &r->cc_pmt, &ptr0, sec, n, 0, 0, cb, ctx);
    }
    /* r->cas and source EMM passthrough are mutually exclusive (see remux_new()) */
    if (r->standalone && (r->cas || find_ca_passthrough(r, 2)) && due(now, &r->last_cat, INTERVAL_PAT_PMT_S)) {
      if (r->cas) {
        n = cas_build_cat(r->cas, sec, sizeof sec);
      } else {
        unsigned char emm_desc[16];
        size_t emm_desc_len = remux_source_emm_descriptor(r, emm_desc, sizeof emm_desc);
        n = emm_desc_len ? psi_build_cat(0, emm_desc, emm_desc_len, sec, sizeof sec) : 0;
      }
      psi_note(tsm, PSI_TABLE_CAT, n);
      if (n)
        ts_packet_emit(OUT_PID_CAT, &r->cc_cat, &ptr0, sec, n, 0, 0, cb, ctx);
    }
  }
  if (r->standalone) {
    if (r->send_sdt && due(now, &r->last_sdt, INTERVAL_SDT_S)) {
      n = psi_build_sdt(0, r->cfg.tsid, r->cfg.onid, r->input.sid, 0x01, r->provider_name, r->service_name, sec, sizeof sec);
      psi_note(tsm, PSI_TABLE_SDT, n);
      if (n)
        ts_packet_emit(OUT_PID_SDT, &r->cc_sdt, &ptr0, sec, n, 0, 0, cb, ctx);
    }
    if (r->send_nit && due(now, &r->last_nit, INTERVAL_NIT_S)) {
      n = psi_build_nit(0, r->cfg.onid, r->cfg.tsid, r->network_name, sec, sizeof sec);
      psi_note(tsm, PSI_TABLE_NIT, n);
      if (n)
        ts_packet_emit(OUT_PID_NIT, &r->cc_nit, &ptr0, sec, n, 0, 0, cb, ctx);
    }
  }
  if (r->send_ait && due(now, &r->last_ait, INTERVAL_AIT_S)) {
    ts_packet_emit(r->pids.ait_pid, &r->cc_ait, &ptr0, r->ait_section, r->ait_section_len, 0, 0, cb, ctx);
    if (tsm)
      tsm->ait_sections_total++;
  }

  if (r->standalone && r->cas) {
    size_t n_vendors = cas_vendor_count(r->cas);
    for (size_t vi = 0; vi < n_vendors; vi++) {
      size_t len;
      if (cas_vendor_ecm_due(r->cas, vi, now, sec, sizeof sec, &len) == 0)
        ts_packet_emit(cas_vendor_ecm_pid(r->cas, vi), &r->cc_ecm[vi], &ptr0, sec, len, 0, 0, cb, ctx);
      while (cas_vendor_next_emm(r->cas, vi, sec, sizeof sec, &len) == 0)
        ts_packet_emit(cas_vendor_emm_pid(r->cas, vi), &r->cc_emm[vi], &ptr0, sec, len, 0, 0, cb, ctx);
    }
  }
}

static void forward_packet(remux_t *r, unsigned out_pid, unsigned char *cc, const unsigned char *pkt188, double now, remux_packet_cb cb, void *ctx) {
  unsigned char out[188];
  /* ISO 13818-1 2.4.3.3: cc must not advance on AFC '10' (adaptation field only, no payload).
     video's PCR pid uses these for pacing - bumping cc on them fakes a discontinuity. */
  int has_payload = (pkt188[3] & 0x10) != 0;
  memcpy(out, pkt188, 188);
  out[1] = (unsigned char)((out[1] & 0xE0) | ((out_pid >> 8) & 0x1F));
  out[2] = (unsigned char)out_pid;
  if (has_payload)
    *cc = (unsigned char)((*cc + 1) & 0x0F);
  out[3] = (unsigned char)((out[3] & 0xF0) | *cc);
  if (r->cas) {
    cas_pcr_tick(r->cas, out_pid, out);
    cas_scramble_packet(r->cas, out_pid, now, out, cb, ctx);
  } else {
    cb(ctx, out);
  }
}

/* key: table_id+section_number. updates if present, appends if room, else drops */
static void eit_queue_put(remux_t *r, unsigned char table_id, unsigned char section_number, const unsigned char *data, size_t len, ts_metrics_t *tsm) {
  for (int i = 0; i < r->eit_queue_count; i++) {
    if (r->eit_queue[i].table_id == table_id && r->eit_queue[i].section_number == section_number) {
      memcpy(r->eit_queue[i].data, data, len);
      r->eit_queue[i].len = len;
      return;
    }
  }
  if (r->eit_queue_count >= EIT_QUEUE_CAP) {
    if (tsm)
      tsm->eit_queue_drops_total++;
    return;
  }
  r->eit_queue[r->eit_queue_count].table_id = table_id;
  r->eit_queue[r->eit_queue_count].section_number = section_number;
  memcpy(r->eit_queue[r->eit_queue_count].data, data, len);
  r->eit_queue[r->eit_queue_count].len = len;
  r->eit_queue_count++;
}

/* non-standalone: reassemble source EIT, filter to own service_id, enqueue */
static void capture_eit_section(remux_t *r, const unsigned char *pkt188, ts_metrics_t *tsm) {
  const unsigned char *pl;
  size_t plen;
  int pusi;
  const unsigned char *sec;
  unsigned service_id;

  if (!tspack_payload(pkt188, &pl, &plen, &pusi))
    return;
  if (!psi_section_asm_feed(&r->eit_asm, pl, plen, pusi))
    return;
  sec = r->eit_asm.buf;
  if (r->eit_asm.len < 8 || r->eit_asm.len > sizeof r->eit_queue[0].data)
    return;
  service_id = ((unsigned)sec[3] << 8) | sec[4];
  if (service_id != r->src_service_id)
    return;
  eit_queue_put(r, sec[0], sec[6], sec, r->eit_asm.len, tsm);
}

/* continuity_counter gap/discontinuity_indicator, source-side. CC only advances on
   payload-bearing packets (ISO 13818-1 2.4.3.3) - same has_payload test as forward_packet() */
static void track_continuity(remux_t *r, unsigned pid, const unsigned char pkt188[188], ts_metrics_t *tsm) {
  int has_payload = (pkt188[3] & 0x10) != 0;
  int has_adapt = (pkt188[3] & 0x20) != 0;
  int disc_flag = has_adapt && pkt188[4] >= 1 && (pkt188[5] & 0x80) != 0;
  unsigned char cc = pkt188[3] & 0x0F;
  unsigned char state = r->cc_state[pid];

  if (disc_flag && tsm)
    tsm->ts_discontinuities++;
  if (!has_payload)
    return;
  if ((state & 0x10) && !disc_flag && tsm && cc != (unsigned char)((state + 1) & 0x0F))
    tsm->ts_continuity_errors++;
  r->cc_state[pid] = (unsigned char)(0x10 | cc);
}

#define TS_PCR_MODULUS (((uint64_t)1 << 33) * 300ULL)
#define TS_PCR_CLOCK_HZ 27000000ULL

/* same plausibility fence as cas_pcr_plausible() (cas.h says remux.c has no business calling
   that one directly - this is our own copy of the same ISO 13818-1 2.4.3.5 arithmetic) */
static int ts_pcr_plausible(uint64_t last_pcr27, uint64_t new_pcr27, double wall_delta_s) {
  uint64_t delta_ticks = (new_pcr27 + TS_PCR_MODULUS - last_pcr27) % TS_PCR_MODULUS;
  double pcr_delta_s = (double)delta_ticks / (double)TS_PCR_CLOCK_HZ;
  return wall_delta_s > 0.0 && pcr_delta_s > 0.0 && pcr_delta_s < 60.0 && pcr_delta_s < wall_delta_s * 5.0 + 0.5 && pcr_delta_s > wall_delta_s * 0.2 - 0.1;
}

static void track_pcr(remux_t *r, double now_s, const unsigned char pkt188[188], ts_metrics_t *tsm) {
  unsigned afc = (pkt188[3] >> 4) & 0x3;
  uint64_t base, pcr27;
  unsigned ext;

  if (afc != 0x2 && afc != 0x3)
    return;
  if (pkt188[4] < 1 || !(pkt188[5] & 0x10) || pkt188[4] < 7)
    return;
  base = ((uint64_t)pkt188[6] << 25) | ((uint64_t)pkt188[7] << 17) | ((uint64_t)pkt188[8] << 9) | ((uint64_t)pkt188[9] << 1) | (pkt188[10] >> 7);
  ext = ((unsigned)(pkt188[10] & 0x01) << 8) | pkt188[11];
  pcr27 = base * 300 + ext;

  if (r->have_last_pcr && tsm && !ts_pcr_plausible(r->last_pcr27, pcr27, now_s - r->last_pcr_wall))
    tsm->pcr_discontinuities++;
  r->last_pcr27 = pcr27;
  r->last_pcr_wall = now_s;
  r->have_last_pcr = 1;
}

void remux_feed(remux_t *r, double now_s, const unsigned char *pkt188, remux_packet_cb cb, void *ctx, ts_metrics_t *tsm) {
  unsigned in_pid;
  int idx;

  send_psi_tables(r, now_s, cb, ctx, tsm);

  if (pkt188[0] != 0x47) {
    if (tsm)
      tsm->ts_sync_errors++;
    return;
  }
  in_pid = tspack_pid(pkt188);
  if (tsm) {
    tsm->ts_packets++;
    track_continuity(r, in_pid, pkt188, tsm);
    if (r->pcr_pid_in && in_pid == r->pcr_pid_in)
      track_pcr(r, now_s, pkt188, tsm);
  }

  if (in_pid == OUT_PID_EIT) {
    if (!r->input.strip_eit) {
      if (r->standalone)
        forward_packet(r, OUT_PID_EIT, &r->cc_eit, pkt188, now_s, cb, ctx);
      else
        capture_eit_section(r, pkt188, tsm);
      if (tsm)
        tsm->remux_packets_total++;
    } else if (tsm) {
      tsm->remux_dropped_packets_total++;
    }
    return;
  }

  idx = find_es(r, in_pid);
  if (idx < 0) {
    if (tsm)
      tsm->remux_dropped_packets_total++;
    return; /* PAT/PMT/SDT/NIT/unrecognized: not carried, we build our own or drop */
  }
  forward_packet(r, r->es[idx].out_pid, &r->cc_es[idx], pkt188, now_s, cb, ctx);
  if (tsm)
    tsm->remux_packets_total++;
}

int remux_get_sdt_info(remux_t *r, psi_sdt_entry_t *out) {
  if (!r->send_sdt)
    return -1;
  out->service_id = r->input.sid;
  out->service_type = 0x01;
  out->provider = r->provider_name;
  out->service_name = r->service_name;
  return 0;
}

size_t remux_emit_eit(remux_t *r, unsigned pid, unsigned char *cc, size_t max_packets, remux_packet_cb cb, void *ctx) {
  unsigned char ptr0 = 0x00;
  size_t n;

  if (r->eit_queue_count == 0)
    return 0;
  n = ts_packet_emit_partial(pid, cc, &ptr0, r->eit_queue[0].data, r->eit_queue[0].len, &r->eit_drain_off, max_packets, cb, ctx);
  if (r->eit_drain_off >= r->eit_queue[0].len) {
    r->eit_queue_count--;
    memmove(&r->eit_queue[0], &r->eit_queue[1], (size_t)r->eit_queue_count * sizeof r->eit_queue[0]);
    r->eit_drain_off = 0;
  }
  return n;
}

int remux_eit_pending(const remux_t *r) { return r->eit_queue_count > 0; }
