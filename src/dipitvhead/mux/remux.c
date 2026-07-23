/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/log.h"
#include "lib/mux/psi_build.h"
#include "lib/mux/tspacket_write.h"

#include "../version.h"

#include "aitbuild.h"
#include "pmtbuild.h"
#include "remux.h"

#define INTERVAL_PAT_PMT_S 0.1
#define INTERVAL_SDT_S 2.0
#define INTERVAL_NIT_S 10.0
#define INTERVAL_AIT_S 0.5

struct remux {
  config_t cfg;
  out_es_t es[OUT_MAX_ES];
  int es_count;
  unsigned pcr_pid_out;

  int send_sdt, send_nit, send_ait;
  char service_name[256], provider_name[PSI_NAME], network_name[256];
  unsigned char ait_pmt_entry[16];
  size_t ait_pmt_entry_len;
  unsigned char ait_section[300];
  size_t ait_section_len;

  unsigned char cc_pat, cc_pmt, cc_sdt, cc_nit, cc_eit, cc_ait;
  unsigned char cc_es[OUT_MAX_ES];

  double last_pat, last_sdt, last_nit, last_ait;
};

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void resolve_sdt(remux_t *r, const psi_t *psi) {
  if (r->cfg.sdt_mode == TABLE_DROP) {
    r->send_sdt = 0;
  } else if (r->cfg.sdt_mode == TABLE_OVERRIDE) {
    snprintf(r->service_name, sizeof r->service_name, "%s", r->cfg.sdt_text);
    snprintf(r->provider_name, sizeof r->provider_name, "%s", TOOL_NAME);
    r->send_sdt = 1;
  } else {
    snprintf(r->service_name, sizeof r->service_name, "%s", psi_service_name(psi));
    snprintf(r->provider_name, sizeof r->provider_name, "%s", psi_provider_name(psi));
    r->send_sdt = r->service_name[0] != '\0';
  }
}

static void resolve_nit(remux_t *r, const psi_t *psi) {
  if (r->cfg.nit_mode == TABLE_DROP) {
    r->send_nit = 0;
  } else if (r->cfg.nit_mode == TABLE_OVERRIDE) {
    snprintf(r->network_name, sizeof r->network_name, "%s", r->cfg.nit_text);
    r->send_nit = 1;
  } else {
    snprintf(r->network_name, sizeof r->network_name, "%s", psi_network_name(psi));
    r->send_nit = r->network_name[0] != '\0';
  }
}

remux_t *remux_new(const config_t *cfg, const psi_t *psi) {
  remux_t *r = calloc(1, sizeof *r);
  int n, count;
  const psi_es_t *in_es;

  if (!r)
    return NULL;
  r->cfg = *cfg;
  in_es = psi_es(psi, &count);
  n = pmtbuild_map_es(in_es, count, psi_pcr_pid(psi), r->es, OUT_MAX_ES, &r->pcr_pid_out);
  if (n <= 0) {
    free(r);
    return NULL;
  }
  r->es_count = n;
  resolve_sdt(r, psi);
  resolve_nit(r, psi);
  r->send_ait = r->cfg.hbbtv_url != NULL;
  if (r->send_ait) {
    r->ait_pmt_entry_len = aitbuild_pmt_entry(0, r->ait_pmt_entry, sizeof r->ait_pmt_entry);
    r->ait_section_len = aitbuild_ait(0, r->cfg.hbbtv_org_id, r->cfg.hbbtv_app_id, r->cfg.hbbtv_url, r->ait_section, sizeof r->ait_section);
    r->send_ait = r->ait_pmt_entry_len && r->ait_section_len;
    if (!r->send_ait)
      log_line("--hbbtv: AIT build failed (url too long?), not sending it");
  }
  r->last_pat = r->last_sdt = r->last_nit = r->last_ait = -1.0;
  return r;
}

void remux_free(remux_t *r) { free(r); }

static int due(double now, double *last, double interval) {
  if (*last < 0.0 || now - *last >= interval) {
    *last = now;
    return 1;
  }
  return 0;
}

/* es[] index for a source pid, or -1 if not carried (dropped) */
static int find_es(const remux_t *r, unsigned in_pid) {
  int i;
  for (i = 0; i < r->es_count; i++)
    if (r->es[i].in_pid == in_pid)
      return i;
  return -1;
}

static void send_psi_tables(remux_t *r, remux_packet_cb cb, void *ctx) {
  unsigned char sec[4096];
  unsigned char ptr0 = 0x00;
  double now = mono();
  size_t n;

  if (due(now, &r->last_pat, INTERVAL_PAT_PMT_S)) {
    n = psi_build_pat(r->cfg.tsid, 0, r->cfg.sid, OUT_PID_PMT, sec, sizeof sec);
    if (n)
      ts_packet_emit(OUT_PID_PAT, &r->cc_pat, &ptr0, sec, n, 0, 0, cb, ctx);
    n = pmtbuild_pmt(0, r->cfg.sid, r->pcr_pid_out, r->es, r->es_count, r->send_ait ? r->ait_pmt_entry : NULL, r->send_ait ? r->ait_pmt_entry_len : 0, sec, sizeof sec);
    if (n)
      ts_packet_emit(OUT_PID_PMT, &r->cc_pmt, &ptr0, sec, n, 0, 0, cb, ctx);
  }
  if (r->send_sdt && due(now, &r->last_sdt, INTERVAL_SDT_S)) {
    n = psi_build_sdt(0, r->cfg.tsid, r->cfg.onid, r->cfg.sid, 0x01, r->provider_name, r->service_name, sec, sizeof sec);
    if (n)
      ts_packet_emit(OUT_PID_SDT, &r->cc_sdt, &ptr0, sec, n, 0, 0, cb, ctx);
  }
  if (r->send_nit && due(now, &r->last_nit, INTERVAL_NIT_S)) {
    n = psi_build_nit(0, r->cfg.onid, r->cfg.tsid, r->network_name, sec, sizeof sec);
    if (n)
      ts_packet_emit(OUT_PID_NIT, &r->cc_nit, &ptr0, sec, n, 0, 0, cb, ctx);
  }
  if (r->send_ait && due(now, &r->last_ait, INTERVAL_AIT_S))
    ts_packet_emit(OUT_PID_AIT, &r->cc_ait, &ptr0, r->ait_section, r->ait_section_len, 0, 0, cb, ctx);
}

static void forward_packet(unsigned out_pid, unsigned char *cc, const unsigned char *pkt188, remux_packet_cb cb, void *ctx) {
  unsigned char out[188];
  memcpy(out, pkt188, 188);
  out[1] = (unsigned char)((out[1] & 0xE0) | ((out_pid >> 8) & 0x1F));
  out[2] = (unsigned char)out_pid;
  *cc = (unsigned char)((*cc + 1) & 0x0F);
  out[3] = (unsigned char)((out[3] & 0xF0) | *cc);
  cb(ctx, out);
}

void remux_feed(remux_t *r, const unsigned char *pkt188, remux_packet_cb cb, void *ctx) {
  unsigned in_pid;
  int idx;

  send_psi_tables(r, cb, ctx);

  if (pkt188[0] != 0x47)
    return;
  in_pid = (((unsigned)pkt188[1] & 0x1F) << 8) | pkt188[2];

  if (in_pid == 0x0012) { /* EIT: passthrough verbatim, own cc track */
    if (!r->cfg.strip_eit)
      forward_packet(OUT_PID_EIT, &r->cc_eit, pkt188, cb, ctx);
    return;
  }

  idx = find_es(r, in_pid);
  if (idx < 0)
    return; /* PAT/PMT/SDT/NIT/unrecognized: not carried, we build our own or drop */
  forward_packet(r->es[idx].out_pid, &r->cc_es[idx], pkt188, cb, ctx);
}
