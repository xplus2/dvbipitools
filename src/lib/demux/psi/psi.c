/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../crc32.h"
#include "../tspack.h"

#include "priv.h"

/* existing candidate for this program on this pmt_pid, or NULL.
   programs may share a PMT PID (ISO/IEC 13818-1 2.4.4.3) */
pmt_cand_t *find_cand(psi_t *c, unsigned prog, unsigned pmt_pid) {
  for (int k = 0; k < c->pmt_cand_count; k++) {
    if (c->pmt_cand[k].pmt_pid == pmt_pid && c->pmt_cand[k].program_number == prog) return &c->pmt_cand[k];
  }
  return NULL;
}

/* first candidate on pid: owns the section assembler for all programs on it */
static pmt_cand_t *pmt_leader(psi_t *c, unsigned pid) {
  for (int k = 0; k < c->pmt_cand_count; k++) {
    if (c->pmt_cand[k].pmt_pid == pid) return &c->pmt_cand[k];
  }
  return NULL;
}

/* assembled section's andidate, NULL: not ours. bogus sections go to a candidate logging the drop */
static pmt_cand_t *pmt_target(psi_t *c, pmt_cand_t *leader, const unsigned char *b, size_t n) {
  unsigned prog;
  if (n < 5 || b[0] != 0x02) return c->pmt_locked && !c->multi_mode ? &c->pmt_cand[c->pmt_lock_idx] : leader;
  prog = ((unsigned)b[3] << 8) | b[4];
  return find_cand(c, prog, leader->pmt_pid);
}

/* precedence: fixed pids (PAT/CAT/NIT/SDT/EIT/OTHER_SI/NULL) > nit > pmt > es > ecm > pcr,
   stamped lowest-first, higher overwrites (PCR often shares video pid). fixed pids stamped
   last, always win: matches psi_classify()'s old hardcoded-first behavior */
void rebuild_class_table(psi_t *c) {
  memset(c->class_by_pid, 0, sizeof c->class_by_pid); /* PID_UNKNOWN == 0 */
  if (c->have_pmt) {
    c->class_by_pid[c->pcr_pid] = PID_PCR;
    for (int i = 0; i < c->ecm_count; i++) c->class_by_pid[c->ecm[i]] = PID_ECM;
    for (int i = 0; i < c->es_count; i++)  c->class_by_pid[c->es[i].pid] = c->es[i].cls;
    c->class_by_pid[c->pmt_pid] = PID_PMT;
  } else if (c->have_pat) {
    c->class_by_pid[c->pmt_pid] = PID_PMT;
  }
  if (c->have_pat && c->nit_pid)
    c->class_by_pid[c->nit_pid] = PID_NIT;
  c->class_by_pid[0x0000] = PID_PAT;
  c->class_by_pid[0x0001] = PID_CAT;
  c->class_by_pid[0x0010] = PID_NIT;
  c->class_by_pid[0x0011] = PID_SDT;
  c->class_by_pid[0x0012] = PID_EIT;
  c->class_by_pid[0x0013] = PID_OTHER_SI;
  c->class_by_pid[0x0014] = PID_OTHER_SI;
  c->class_by_pid[0x1FFF] = PID_NULL;
}

psi_t *psi_new(void) {
  psi_t *c = calloc(1, sizeof(psi_t));
  if (c) rebuild_class_table(c); /* seeds fixed pids before any packet arrives */
  return c;
}

void psi_free(psi_t *c) { free(c); }

void psi_set_label(psi_t *c, const char *label) {
  if (!c) return;
  if (label && *label) snprintf(c->tag, sizeof c->tag, "psi %s", label);
  else c->tag[0] = 0;
}

const char *psi_tag(const psi_t *c) { return c->tag[0] ? c->tag : "psi"; }

/* records that pmt_cand[k] just resolved: locks onto it (single-mode, first resolution only),
   marks it resolved in multi-mode */
static void note_pmt_resolved(psi_t *c, int k) {
  if (!c->pmt_locked) {
    c->pmt_locked = 1;
    c->pmt_lock_idx = k;
  }
  if (c->multi_mode) c->multi[k].resolved = 1;
}

void psi_obs_init(psi_obs_t *o, const double *now) {
  memset(o, 0, sizeof *o);
  o->now = now;
  for (int i = 0; i < PSI_OBS_COUNT; i++) o->t[i].version = -1;
}

void psi_set_observer(psi_t *c, psi_obs_t *o) { c->obs = o; }

double psi_obs_oldest_section(const psi_obs_t *o, psi_obs_id_t id) {
  int k = id == PSI_OBS_NIT;
  double oldest = 0.0;
  for (unsigned i = 0; i < o->sec_count[k]; i++) {
    if (o->sec_last[k][i] == 0.0) return 0.0;
    if (oldest == 0.0 || o->sec_last[k][i] < oldest) oldest = o->sec_last[k][i];
  }
  return oldest;
}

double psi_obs_oldest_other(const psi_obs_t *o, psi_obs_id_t id) {
  int k = id == PSI_OBS_NIT_OTHER;
  double oldest = 0.0;
  for (unsigned i = 0; i < o->other_n[k]; i++)
    if (oldest == 0.0 || o->other[k][i].last < oldest) oldest = o->other[k][i].last;
  return oldest;
}

double psi_pmt_oldest_seen(const psi_t *c) {
  double oldest = 0.0;
  for (int k = 0; k < c->pmt_cand_count; k++) {
    if (c->pmt_cand[k].obs_last == 0.0) return 0.0;
    if (oldest == 0.0 || c->pmt_cand[k].obs_last < oldest) oldest = c->pmt_cand[k].obs_last;
  }
  return oldest;
}

static void obs_note(psi_t *c, psi_obs_id_t id, const unsigned char *b, size_t n, unsigned tid, pmt_cand_t *cand) {
  psi_obs_stat_t *s;
  if (!c->obs || n < 8 || b[0] != tid) return;
  s = &c->obs->t[id];
  if (cand) cand->obs_last = *c->obs->now;
  s->last_seen = *c->obs->now;
  if (id == PSI_OBS_BAT && c->obs->crc_bat && n >= 12 && crc32_mpeg(b, n) != 0) s->crc_errors++;
  if (id == PSI_OBS_SDT_OTHER || id == PSI_OBS_NIT_OTHER) {
    int k = id == PSI_OBS_NIT_OTHER;
    uint32_t key = ((uint32_t)b[3] << 16) | ((uint32_t)b[4] << 8) | b[6];
    unsigned i;
    i = 0;
    while (i < c->obs->other_n[k] && c->obs->other[k][i].key != key) i++;
    if (i == c->obs->other_n[k] && i < PSI_OBS_OTHER_MAX) {
      c->obs->other[k][i].key = key;
      c->obs->other_n[k]++;
    }
    if (i < PSI_OBS_OTHER_MAX) c->obs->other[k][i].last = *c->obs->now;
  }
  if (id == PSI_OBS_SDT || id == PSI_OBS_NIT) {
    int k = id == PSI_OBS_NIT;
    c->obs->sec_last[k][b[6]] = *c->obs->now;
    c->obs->sec_count[k] = (unsigned)b[7] + 1;
  }
}

static void feed_pmt_pid(psi_t *c, unsigned pid, const unsigned char *pkt, const unsigned char *pl, size_t plen, int pusi) {
  pmt_cand_t *lead = pmt_leader(c, pid);
  psi_section_asm_t *a;
  if (!lead || !psi_section_asm_cc(&lead->asm_, pkt)) return;
  a = &lead->asm_;
  for (int got = psi_section_asm_feed(a, pl, plen, pusi); got; got = psi_section_asm_next(a, pl, plen)) {
    pmt_cand_t *cand;
    if (a->expect && a->buf[0] != 0x02) { /* ECM etc sharing the PMT pid */
      if (!lead->foreign_logged) {
        log_line("%s: non-PMT sections on PMT pid 0x%04x (table_id 0x%02x), ignoring", psi_tag(c), pid, a->buf[0]);
        lead->foreign_logged = 1;
      }
      continue;
    }
    cand = pmt_target(c, lead, a->buf, a->expect);
    if (!cand) continue;
    if (c->pmt_locked && !c->multi_mode && cand != &c->pmt_cand[c->pmt_lock_idx]) continue;
    obs_note(c, PSI_OBS_PMT, a->buf, a->expect, 0x02, cand);
    if (parse_pmt(c, cand, a->buf, a->expect)) note_pmt_resolved(c, (int)(cand - c->pmt_cand));
  }
}

void psi_feed(psi_t *c, const unsigned char *pkt) {
  unsigned pid;
  int pusi;
  size_t plen;
  const unsigned char *pl;

  if (pkt[0] != 0x47) return;
  pid = tspack_pid(pkt);
  if (!tspack_payload(pkt, &pl, &plen, &pusi)) return;
  switch (pid) {
    case TS_PID_PAT:
      if (!psi_section_asm_cc(&c->pat, pkt)) return;
      for (int got = psi_section_asm_feed(&c->pat, pl, plen, pusi); got; got = psi_section_asm_next(&c->pat, pl, plen)) {
        obs_note(c, PSI_OBS_PAT, c->pat.buf, c->pat.expect, 0x00, NULL);
        parse_pat(c);
      }
      return;
    case TS_PID_NIT:
      if (!psi_section_asm_cc(&c->nit, pkt)) return;
      for (int got = psi_section_asm_feed(&c->nit, pl, plen, pusi); got; got = psi_section_asm_next(&c->nit, pl, plen)) {
        obs_note(c, PSI_OBS_NIT, c->nit.buf, c->nit.expect, 0x40, NULL);
        obs_note(c, PSI_OBS_NIT_OTHER, c->nit.buf, c->nit.expect, 0x41, NULL);
        parse_nit(c);
      }
      return;
    case TS_PID_SDT:
      if (!psi_section_asm_cc(&c->sdt, pkt)) return;
      for (int got = psi_section_asm_feed(&c->sdt, pl, plen, pusi); got; got = psi_section_asm_next(&c->sdt, pl, plen)) {
        obs_note(c, PSI_OBS_SDT, c->sdt.buf, c->sdt.expect, 0x42, NULL);
        obs_note(c, PSI_OBS_SDT_OTHER, c->sdt.buf, c->sdt.expect, 0x46, NULL);
        obs_note(c, PSI_OBS_BAT, c->sdt.buf, c->sdt.expect, 0x4A, NULL);
        parse_sdt(c);
      }
      return;
    case TS_PID_CAT:
      if (!psi_section_asm_cc(&c->cat, pkt)) return;
      for (int got = psi_section_asm_feed(&c->cat, pl, plen, pusi); got; got = psi_section_asm_next(&c->cat, pl, plen)) {
        obs_note(c, PSI_OBS_CAT, c->cat.buf, c->cat.expect, 0x01, NULL);
        parse_cat(c);
      }
      return;
    default:
      break;
  }

  if (c->pmt_locked && !c->multi_mode) {
    if (pid == c->pmt_pid) feed_pmt_pid(c, pid, pkt, pl, plen, pusi);
  } else if (c->have_pat) {
    feed_pmt_pid(c, pid, pkt, pl, plen, pusi);
  }
}

int psi_wants_pid(const psi_t *c, unsigned pid) {
  switch (pid) {
    case TS_PID_PAT:
    case TS_PID_NIT:
    case TS_PID_SDT:
    case TS_PID_CAT:
      return 1;
    default:
      break;
  }
  if (c->pmt_locked && !c->multi_mode) return pid == c->pmt_pid;
  return c->have_pat && pid < 8192 && c->pmt_wanted[pid];
}

void psi_select_pmt_pid(psi_t *c, unsigned pmt_pid) { c->preferred_pmt_pid = pmt_pid; }

void psi_enable_multi_program(psi_t *c) { c->multi_mode = 1; }

const psi_multi_program_t *psi_multi_programs(const psi_t *c, int *count) {
  if (count) *count = c->multi_count;
  return c->multi;
}

const psi_program_t *psi_pat_programs(const psi_t *c, int *count) {
  if (count) *count = c->pat_program_count;
  return c->pat_programs;
}

int psi_have_pat(const psi_t *c) { return c->have_pat; }
int psi_have_pmt(const psi_t *c) { return c->have_pmt; }
unsigned long psi_pmt_parsed(const psi_t *c) { return c->pmt_parsed; }
int psi_have_sdt(const psi_t *c) { return c->have_sdt; }
int psi_have_cat(const psi_t *c) { return c->have_cat; }
int psi_ready(const psi_t *c) { return c->have_pat && c->have_pmt; }

unsigned psi_program_number(const psi_t *c) { return c->program_number; }
unsigned psi_pmt_pid(const psi_t *c) { return c->pmt_pid; }
unsigned psi_pcr_pid(const psi_t *c) { return c->pcr_pid; }
unsigned psi_nit_pid(const psi_t *c) { return c->nit_pid; }
unsigned psi_transport_stream_id(const psi_t *c) { return c->tsid; }
unsigned psi_original_network_id(const psi_t *c) { return c->onid; }
unsigned psi_emm_pid(const psi_t *c) { return c->emm_pid; }
unsigned psi_ca_system_id(const psi_t *c) { return c->ca_system_id; }
unsigned char psi_scrambling_mode(const psi_t *c) { return c->scrambling_mode; }
unsigned psi_pmt_ca_system_id(const psi_t *c) { return c->pmt_ca_system_id; }
unsigned psi_pmt_ca_pid(const psi_t *c) { return c->pmt_ca_pid; }

const psi_es_t *psi_es(const psi_t *c, int *count) {
  if (count) *count = c->es_count;
  return c->es;
}

int psi_audio_count(const psi_t *c) { return c->audio_count; }

const char *psi_service_name(const psi_t *c) { return c->service_name; }
const char *psi_provider_name(const psi_t *c) { return c->provider_name; }
const char *psi_network_name(const psi_t *c) { return c->network_name; }

pid_class_t psi_classify(const psi_t *c, unsigned pid) { return c->class_by_pid[pid]; }

unsigned psi_service_of_pid(const psi_t *c, unsigned pid) { return c->service_by_pid[pid]; }

const unsigned char *psi_pat_section(const psi_t *c, size_t *len) {
  if (!c->have_pat) {
    if (len) *len = 0;
    return NULL;
  }
  if (len) *len = c->pat.expect;
  return c->pat.buf;
}

const unsigned char *psi_pmt_section(const psi_t *c, size_t *len) {
  if (!c->have_pmt || !c->pmt_locked) {
    if (len) *len = 0;
    return NULL;
  }
  if (len) *len = c->pmt_sec_len;
  return c->pmt_sec;
}

const char *pid_class_name(pid_class_t k) {
  switch (k) {
    case PID_PAT:       return "PAT";
    case PID_CAT:       return "CAT";
    case PID_PMT:       return "PMT";
    case PID_NIT:       return "NIT";
    case PID_SDT:       return "SDT";
    case PID_EIT:       return "EIT";
    case PID_OTHER_SI:  return "SI";
    case PID_NULL:      return "null";
    case PID_PCR:       return "PCR";
    case PID_VIDEO:     return "video";
    case PID_AUDIO:     return "audio";
    case PID_TELETEXT:  return "teletext";
    case PID_SUBTITLE:  return "subtitle";
    case PID_AIT:       return "AIT";
    case PID_ECM:       return "ECM";
    case PID_DATA:      return "data";
    case PID_LCEVC:     return "LCEVC";
    case PID_UNKNOWN:   return "unknown";
  }
  return "unknown";
}

const char *codec_name(codec_t k) {
  switch (k) {
    case CODEC_MPEG2V:    return "mpeg2video";
    case CODEC_H264:      return "h264";
    case CODEC_HEVC:      return "hevc";
    case CODEC_VVC:       return "vvc";
    case CODEC_AV1:       return "av1";
    case CODEC_MP2A:      return "mp2";
    case CODEC_AAC:       return "aac";
    case CODEC_AAC_LATM:  return "aac_latm";
    case CODEC_AC3:       return "ac3";
    case CODEC_EAC3:      return "eac3";
    case CODEC_OPUS:      return "opus";
    case CODEC_LCEVC:     return "lcevc";
    case CODEC_DTS:       return "dts";
    case CODEC_DTS_HD:    return "dts_hd";
    case CODEC_DTS_HD_MA: return "dts_hd_ma";
    case CODEC_TRUEHD:    return "truehd";
    case CODEC_AC4:       return "ac4";
    case CODEC_NONE:      return "none";
  }
  return "none";
}
