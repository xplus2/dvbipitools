/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "../../helper/log.h"
#include "../crc32.h"
#include "../tspack.h"

#include "priv.h"

void obs_version(psi_t *c, psi_obs_id_t id, const unsigned char *b, pmt_cand_t *cand) {
  int *ver;
  int v;
  if (!c->obs || !(b[5] & 1)) return;
  ver = cand ? &cand->obs_ver : &c->obs->t[id].version;
  v = (b[5] >> 1) & 0x1F;
  if (*ver >= 0 && *ver != v) c->obs->t[id].changes++;
  *ver = v;
}

static int section_bad(psi_t *c, psi_obs_id_t id, const unsigned char *b, size_t n) {
  if (crc32_mpeg(b, n) == 0) return 0;
  if (c->obs) c->obs->t[id].crc_errors++;
  return 1;
}

/* NULL on section ok, else short reason. count crc errs */
static const char *section_fault(psi_t *c, psi_obs_id_t id, const unsigned char *b, size_t n, size_t min_len, unsigned char tid) {
  if (n < min_len) return "too short";
  if (b[0] != tid) return "wrong table_id";
  if (section_bad(c, id, b, n)) return "crc";
  return NULL;
}

/* registers pid as a new PMT candidate for program prog (assumes prog/pid pair isn't already a
   candidate). also mirrors into c->multi[] when multi-program mode is on */
static void add_pmt_candidate(psi_t *c, unsigned prog, unsigned pid) {
  if (c->pmt_cand_count < PSI_MAX_PROGRAMS) {
    pmt_cand_t *cand = &c->pmt_cand[c->pmt_cand_count];
    memset(cand, 0, sizeof *cand);
    cand->obs_ver = -1;
    cand->program_number = prog;
    cand->pmt_pid = pid;
    c->pmt_cand_count++;
    c->pmt_wanted[pid] = 1;
    if (c->multi_mode) {
      psi_multi_program_t *m = &c->multi[c->multi_count];
      memset(m, 0, sizeof *m);
      m->program_number = prog;
      m->pmt_pid = pid;
      c->multi_count++;
    }
  } else if (!c->pmt_cand_overflow_logged) {
    log_line("%s: more than %d PMT candidates, dropping the rest", psi_tag(c), PSI_MAX_PROGRAMS);
    c->pmt_cand_overflow_logged = 1;
  }
}

void parse_pat(psi_t *c) {
  const unsigned char *b = c->pat.buf;
  size_t n = c->pat.expect;
  size_t end;
  const char *fault = section_fault(c, PSI_OBS_PAT, b, n, 12, 0x00);
  if (fault) {
    log_throttled(&c->pat_drop_throttle, LOG_THROTTLE_WINDOW_S, "%s: PAT section dropped: %s (table_id 0x%02x, len %zu)", psi_tag(c), fault, b[0], n);
    return;
  }
  obs_version(c, PSI_OBS_PAT, b, NULL);
  c->nit_pid = 0;
  c->pat_program_count = 0;
  /* pmt_cand[] deliberately not reset here: a candidate's PMT may still be
   * mid-assembly, PAT repeats far more often than that takes */
  c->tsid = ((unsigned)b[3] << 8) | b[4];
  end = n - 4;
  for (size_t i = 8; i + 4 <= end; i += 4) {
    unsigned prog = ((unsigned)b[i] << 8) | b[i + 1];
    unsigned pid = (((unsigned)b[i + 2] & 0x1F) << 8) | b[i + 3];
    if (prog == 0) {
      c->nit_pid = pid;
      continue;
    }
    if (c->pat_program_count < PSI_MAX_PROGRAMS) {
      c->pat_programs[c->pat_program_count].program_number = prog;
      c->pat_programs[c->pat_program_count].pmt_pid = pid;
      c->pat_program_count++;
    } else if (!c->pat_program_overflow_logged) {
      log_line("%s: PAT has more than %d programs, dropping the rest", psi_tag(c), PSI_MAX_PROGRAMS);
      c->pat_program_overflow_logged = 1;
    }
    if (c->pmt_locked && !c->multi_mode) continue;
    if (c->preferred_pmt_pid && pid != c->preferred_pmt_pid) continue;
    if (!find_cand(c, prog, pid)) add_pmt_candidate(c, prog, pid);
  }
  if (c->pat_program_count < PSI_MAX_PROGRAMS) c->pat_program_overflow_logged = 0;
  if (c->pmt_cand_count < PSI_MAX_PROGRAMS) c->pmt_cand_overflow_logged = 0;
  c->have_pat = 1;
  rebuild_class_table(c);
}

static int priority_desc(unsigned char tag) {
  switch (tag) {
    case 0x0A: case 0x56: case 0x59: case 0x6A: case 0x7A:
      return 1;
    default:
      return 0;
  }
}

static void capture_es_desc(psi_es_t *e, const unsigned char *desc, size_t len) {
  if (len <= sizeof e->desc) {
    memcpy(e->desc, desc, len);
    e->desc_len = len;
    return;
  }
  for (int pass = 0; pass < 2; pass++) {
    for (size_t i = 0; i + 2 <= len; i += 2 + (size_t)desc[i + 1]) {
      size_t dl = 2 + (size_t)desc[i + 1];
      if (i + dl > len) break;
      if (desc[i] == 0x09 || priority_desc(desc[i]) != (pass == 0)) continue;
      if (e->desc_len + dl > sizeof e->desc) continue;
      memcpy(e->desc + e->desc_len, desc + i, dl);
      e->desc_len += dl;
    }
  }
}

/* 1 if this candidate's section parsed into a valid, complete PMT */
int parse_pmt(psi_t *c, pmt_cand_t *cand, const unsigned char *b, size_t n) {
  size_t i;
  size_t end;
  size_t pil;
  size_t l;
  unsigned prog;
  int hdmv = 0;
  const unsigned char *ca;
  const char *fault = section_fault(c, PSI_OBS_PMT, b, n, 16, 0x02);
  if (fault) {
    log_throttled(&c->pmt_drop_throttle, LOG_THROTTLE_WINDOW_S, "%s: PMT section dropped: %s (pid 0x%04x, program %u, table_id 0x%02x, len %zu)", psi_tag(c), fault, cand->pmt_pid, cand->program_number, b[0], n);
    return 0;
  }
  prog = ((unsigned)b[3] << 8) | b[4];
  if (prog != cand->program_number) return 0;
  if (!c->pmt_locked || cand == &c->pmt_cand[c->pmt_lock_idx]) {
    memcpy(c->pmt_sec, b, n);
    c->pmt_sec_len = n;
  }
  obs_version(c, PSI_OBS_PMT, b, cand);
  c->program_number = prog;
  c->pmt_pid = cand->pmt_pid;
  c->pcr_pid = (((unsigned)b[8] & 0x1F) << 8) | b[9];
  pil = tspack_length12(b + 10);
  c->es_count = 0;
  c->audio_count = 0;
  c->ecm_count = 0;
  c->scrambling_mode = 0;
  c->pmt_ca_system_id = 0;
  c->pmt_ca_pid = 0;
  if (12 + pil <= n) {
    const unsigned char *sd;
    ca = find_desc(b + 12, pil, 0x09, &l);
    if (ca && l >= 4) {
      c->pmt_ca_system_id = ((unsigned)ca[0] << 8) | ca[1];
      c->pmt_ca_pid = (((unsigned)ca[2] & 0x1F) << 8) | ca[3];
      add_ecm(c, c->pmt_ca_pid);
    }
    sd = find_desc(b + 12, pil, 0x65, &l);
    if (sd && l >= 1) c->scrambling_mode = sd[0];
    sd = find_desc(b + 12, pil, 0x05, &l);
    if (sd && l >= 4 && (!memcmp(sd, "HDMV", 4) || !memcmp(sd, "HDPR", 4))) hdmv = 1;
  }

  end = n - 4;
  i = 12 + pil;
  while (i + 5 <= end && c->es_count < PSI_MAX_ES) {
    psi_es_t *e = &c->es[c->es_count];
    size_t esil = tspack_length12(b + i + 3);
    const unsigned char *desc = b + i + 5;
    if (i + 5 + esil > end) break;
    memset(e, 0, sizeof *e);
    e->stream_type = b[i];
    e->pid = (((unsigned)b[i + 1] & 0x1F) << 8) | b[i + 2];
    classify(e, desc, esil, hdmv);
    capture_es_desc(e, desc, esil);
    ca = find_desc(desc, esil, 0x09, &l);
    if (ca && l >= 4) {
      e->ca_pid = (((unsigned)ca[2] & 0x1F) << 8) | ca[3];
      e->ca_system_id = ((unsigned)ca[0] << 8) | ca[1];
      add_ecm(c, e->ca_pid);
    }
    c->es_count++;
    i += 5 + esil;
  }
  if (c->es_count >= PSI_MAX_ES && i + 5 <= end) {
    if (!c->es_overflow_logged) {
      log_line("%s: PMT for program %u has more than %d ES entries, dropping the rest", psi_tag(c), prog, PSI_MAX_ES);
      c->es_overflow_logged = 1;
    }
  } else {
    c->es_overflow_logged = 0;
  }
  for (int k = 0; k < c->es_count; k++) {
    if (c->es[k].cls == PID_AUDIO) c->es[k].audio_index = ++c->audio_count;
  }
  link_lcevc(c->es, c->es_count);
  if (cand->last_pcr_pid) c->service_by_pid[cand->last_pcr_pid] = 0;
  for (int k = 0; k < cand->last_es_count; k++) c->service_by_pid[cand->last_es_pid[k]] = 0;
  for (int k = 0; k < cand->last_ecm_count; k++) c->service_by_pid[cand->last_ecm_pid[k]] = 0;
  c->service_by_pid[cand->pmt_pid] = (uint16_t)prog;
  c->service_by_pid[c->pcr_pid] = (uint16_t)prog;
  for (int k = 0; k < c->es_count; k++) c->service_by_pid[c->es[k].pid] = (uint16_t)prog;
  for (int k = 0; k < c->ecm_count; k++) c->service_by_pid[c->ecm[k]] = (uint16_t)prog;
  cand->last_pcr_pid = c->pcr_pid;
  cand->last_es_count = c->es_count;
  for (int k = 0; k < c->es_count; k++) cand->last_es_pid[k] = c->es[k].pid;
  cand->last_ecm_count = c->ecm_count;
  for (int k = 0; k < c->ecm_count; k++) cand->last_ecm_pid[k] = (uint16_t)c->ecm[k];
  c->have_pmt = 1;
  c->pmt_parsed++;
  rebuild_class_table(c);
  return 1;
}

/* index into pmt_cand[]/multi[] for program_number, -1 if unknown */
static int find_multi_index(const psi_t *c, unsigned program_number) {
  for (int k = 0; k < c->pmt_cand_count; k++) {
    if (c->pmt_cand[k].program_number == program_number) return k;
  }
  return -1;
}

void parse_sdt(psi_t *c) {
  const unsigned char *b = c->sdt.buf;
  size_t n = c->sdt.expect;
  size_t i;
  size_t end;
  const char *fault = section_fault(c, PSI_OBS_SDT, b, n, 12, 0x42);
  if (fault) {
    log_throttled(&c->sdt_drop_throttle, LOG_THROTTLE_WINDOW_S, "%s: SDT section dropped: %s (table_id 0x%02x, len %zu)", psi_tag(c), fault, b[0], n);
    return;
  }
  obs_version(c, PSI_OBS_SDT, b, NULL);
  c->onid = ((unsigned)b[8] << 8) | b[9];
  end = n - 4;
  i = 11;
  while (i + 5 <= end) {
    unsigned sid = ((unsigned)b[i] << 8) | b[i + 1];
    const unsigned char *d = b + i + 5;
    size_t dll = tspack_length12(b + i + 3);
    if (i + 5 + dll > end) break;
    if (sid == c->program_number) decode_service_desc(d, dll, c->provider_name, c->service_name);
    if (c->multi_mode) {
      int k = find_multi_index(c, sid);
      if (k >= 0) decode_service_desc(d, dll, c->multi[k].provider_name, c->multi[k].service_name);
    }
    i += 5 + dll;
  }
  c->have_sdt = 1;
}

void parse_nit(psi_t *c) {
  const unsigned char *b = c->nit.buf;
  size_t n = c->nit.expect;
  size_t ndl;
  size_t l;
  const unsigned char *nn;
  const char *fault = section_fault(c, PSI_OBS_NIT, b, n, 12, 0x40);
  if (fault) {
    log_throttled(&c->nit_drop_throttle, LOG_THROTTLE_WINDOW_S, "%s: NIT section dropped: %s (table_id 0x%02x, len %zu)", psi_tag(c), fault, b[0], n);
    return;
  }
  obs_version(c, PSI_OBS_NIT, b, NULL);
  ndl = tspack_length12(b + 8);
  if (10 + ndl > n) {
    log_throttled(&c->nit_drop_throttle, LOG_THROTTLE_WINDOW_S, "%s: NIT network descriptor loop overruns section, dropped", psi_tag(c));
    return;
  }
  nn = find_desc(b + 10, ndl, 0x40, &l);
  if (nn) copy_name(c->network_name, sizeof c->network_name, nn, l);
  c->have_nit = 1;
}

/* ISO/IEC 13818-1 table 2-30: table_id + 7 more header bytes (same shape as PAT's program loop header),
   plain descriptor loop up to CRC. takes first CA_descriptor found (tag 0x09), single-CAS assumption */
void parse_cat(psi_t *c) {
  const unsigned char *b = c->cat.buf;
  size_t n = c->cat.expect;
  size_t l;
  const unsigned char *ca;
  const char *fault = section_fault(c, PSI_OBS_CAT, b, n, 12, 0x01);
  if (fault) {
    log_throttled(&c->cat_drop_throttle, LOG_THROTTLE_WINDOW_S, "%s: CAT section dropped: %s (table_id 0x%02x, len %zu)", psi_tag(c), fault, b[0], n);
    return;
  }
  obs_version(c, PSI_OBS_CAT, b, NULL);
  c->emm_pid = 0;
  c->ca_system_id = 0;
  ca = find_desc(b + 8, n - 12, 0x09, &l);
  if (ca && l >= 4) {
    c->ca_system_id = ((unsigned)ca[0] << 8) | ca[1];
    c->emm_pid = (((unsigned)ca[2] & 0x1F) << 8) | ca[3];
  }
  c->have_cat = 1;
}
