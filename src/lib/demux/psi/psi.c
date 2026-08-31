/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "../tspack.h"

#include "priv.h"

/* existing candidate for this pmt_pid, or NULL */
pmt_cand_t *find_cand(psi_t *c, unsigned pmt_pid) {
  for (int k = 0; k < c->pmt_cand_count; k++)
    if (c->pmt_cand[k].pmt_pid == pmt_pid)
      return &c->pmt_cand[k];
  return NULL;
}

/* precedence: fixed pids (PAT/CAT/NIT/SDT/EIT/OTHER_SI/NULL) > nit > pmt > es > ecm > pcr,
   stamped lowest-first, higher overwrites (PCR often shares video pid). fixed pids stamped
   last, always win: matches psi_classify()'s old hardcoded-first behavior */
void rebuild_class_table(psi_t *c) {
  memset(c->class_by_pid, 0, sizeof c->class_by_pid); /* PID_UNKNOWN == 0 */
  if (c->have_pmt) {
    c->class_by_pid[c->pcr_pid] = PID_PCR;
    for (int i = 0; i < c->ecm_count; i++)
      c->class_by_pid[c->ecm[i]] = PID_ECM;
    for (int i = 0; i < c->es_count; i++)
      c->class_by_pid[c->es[i].pid] = c->es[i].cls;
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
  if (c)
    rebuild_class_table(c); /* seeds fixed pids before any packet arrives */
  return c;
}

void psi_free(psi_t *c) { free(c); }

/* records that pmt_cand[k] just resolved: locks onto it (single-mode, first resolution
   only) and marks it resolved in multi-mode */
static void note_pmt_resolved(psi_t *c, int k) {
  if (!c->pmt_locked) {
    c->pmt_locked = 1;
    c->pmt_lock_idx = k;
  }
  if (c->multi_mode)
    c->multi[k].resolved = 1;
}

void psi_feed(psi_t *c, const unsigned char *pkt) {
  unsigned pid;
  int pusi;
  size_t plen;
  const unsigned char *pl;

  if (pkt[0] != 0x47)
    return;
  pid = tspack_pid(pkt);
  if (!tspack_payload(pkt, &pl, &plen, &pusi))
    return;

  switch (pid) {
    case TS_PID_PAT:
      if (psi_section_asm_feed(&c->pat, pl, plen, pusi))
        parse_pat(c);
      return;
    case TS_PID_NIT:
      if (psi_section_asm_feed(&c->nit, pl, plen, pusi))
        parse_nit(c);
      return;
    case TS_PID_SDT:
      if (psi_section_asm_feed(&c->sdt, pl, plen, pusi))
        parse_sdt(c);
      return;
    case TS_PID_CAT:
      if (psi_section_asm_feed(&c->cat, pl, plen, pusi))
        parse_cat(c);
      return;
    default:
      break;
  }

  if (c->pmt_locked && !c->multi_mode) {
    if (pid == c->pmt_pid && psi_section_asm_feed(&c->pmt_cand[c->pmt_lock_idx].asm_, pl, plen, pusi))
      parse_pmt(c, &c->pmt_cand[c->pmt_lock_idx]);
  } else if (c->have_pat) {
    for (int k = 0; k < c->pmt_cand_count; k++) {
      pmt_cand_t *cand = &c->pmt_cand[k];
      if (cand->pmt_pid != pid)
        continue;
      if (psi_section_asm_feed(&cand->asm_, pl, plen, pusi) && parse_pmt(c, cand))
        note_pmt_resolved(c, k);
      break;
    }
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
  if (c->pmt_locked && !c->multi_mode)
    return pid == c->pmt_pid;
  return c->have_pat && pid < 8192 && c->pmt_wanted[pid];
}

void psi_select_pmt_pid(psi_t *c, unsigned pmt_pid) { c->preferred_pmt_pid = pmt_pid; }

void psi_enable_multi_program(psi_t *c) { c->multi_mode = 1; }

const psi_multi_program_t *psi_multi_programs(const psi_t *c, int *count) {
  if (count)
    *count = c->multi_count;
  return c->multi;
}

const psi_program_t *psi_pat_programs(const psi_t *c, int *count) {
  if (count)
    *count = c->pat_program_count;
  return c->pat_programs;
}

int psi_have_pat(const psi_t *c) { return c->have_pat; }
int psi_have_pmt(const psi_t *c) { return c->have_pmt; }
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
  if (count)
    *count = c->es_count;
  return c->es;
}

int psi_audio_count(const psi_t *c) { return c->audio_count; }

const char *psi_service_name(const psi_t *c) { return c->service_name; }
const char *psi_provider_name(const psi_t *c) { return c->provider_name; }
const char *psi_network_name(const psi_t *c) { return c->network_name; }

pid_class_t psi_classify(const psi_t *c, unsigned pid) { return c->class_by_pid[pid]; }

const unsigned char *psi_pat_section(const psi_t *c, size_t *len) {
  if (!c->have_pat) {
    if (len)
      *len = 0;
    return NULL;
  }
  if (len)
    *len = c->pat.expect;
  return c->pat.buf;
}

const unsigned char *psi_pmt_section(const psi_t *c, size_t *len) {
  if (!c->have_pmt || !c->pmt_locked) {
    if (len)
      *len = 0;
    return NULL;
  }
  if (len)
    *len = c->pmt_cand[c->pmt_lock_idx].asm_.expect;
  return c->pmt_cand[c->pmt_lock_idx].asm_.buf;
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
    case PID_UNKNOWN:   return "unknown";
  }
  return "unknown";
}

const char *codec_name(codec_t k) {
  switch (k) {
    case CODEC_MPEG2V:    return "mpeg2video";
    case CODEC_H264:      return "h264";
    case CODEC_HEVC:      return "hevc";
    case CODEC_MP2A:      return "mp2";
    case CODEC_AAC:       return "aac";
    case CODEC_AAC_LATM:  return "aac_latm";
    case CODEC_AC3:       return "ac3";
    case CODEC_EAC3:      return "eac3";
    case CODEC_NONE:      return "none";
  }
  return "none";
}
