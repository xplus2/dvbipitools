/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "../../log.h"
#include "../crc32.h"
#include "../tspack.h"

#include "priv.h"

void parse_pat(psi_t *c) {
  const unsigned char *b = c->pat.buf;
  size_t n = c->pat.expect, i, end;
  if (n < 12 || b[0] != 0x00 || crc32_mpeg(b, n) != 0)
    return;
  c->nit_pid = 0;
  c->pat_program_count = 0;
  /* pmt_cand[] is deliberately NOT reset here: a candidate's PMT may still
   * be mid-assembly, and the PAT repeats far more often than that takes. */
  c->tsid = ((unsigned)b[3] << 8) | b[4];
  end = n - 4;
  for (i = 8; i + 4 <= end; i += 4) {
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
      log_line("psi: PAT has more than %d programs, dropping the rest", PSI_MAX_PROGRAMS);
      c->pat_program_overflow_logged = 1;
    }
    if (c->pmt_locked)
      continue;
    if (c->preferred_pmt_pid && pid != c->preferred_pmt_pid)
      continue;
    if (!find_cand(c, pid)) {
      if (c->pmt_cand_count < PSI_MAX_PROGRAMS) {
        pmt_cand_t *cand = &c->pmt_cand[c->pmt_cand_count];
        memset(cand, 0, sizeof *cand);
        cand->program_number = prog;
        cand->pmt_pid = pid;
        c->pmt_cand_count++;
        if (c->multi_mode) {
          psi_multi_program_t *m = &c->multi[c->multi_count];
          memset(m, 0, sizeof *m);
          m->program_number = prog;
          m->pmt_pid = pid;
          c->multi_count++;
        }
      } else if (!c->pmt_cand_overflow_logged) {
        log_line("psi: more than %d PMT candidates, dropping the rest", PSI_MAX_PROGRAMS);
        c->pmt_cand_overflow_logged = 1;
      }
    }
  }
  if (c->pat_program_count < PSI_MAX_PROGRAMS)
    c->pat_program_overflow_logged = 0;
  if (c->pmt_cand_count < PSI_MAX_PROGRAMS)
    c->pmt_cand_overflow_logged = 0;
  c->have_pat = 1;
  rebuild_class_table(c);
}

/* 1 if this candidate's section parsed into a valid, complete PMT */
int parse_pmt(psi_t *c, pmt_cand_t *cand) {
  const unsigned char *b = cand->asm_.buf;
  size_t n = cand->asm_.expect, i, end, pil, l;
  unsigned prog;
  const unsigned char *ca;
  int k;
  if (n < 16 || b[0] != 0x02 || crc32_mpeg(b, n) != 0)
    return 0;
  prog = ((unsigned)b[3] << 8) | b[4];
  if (prog != cand->program_number)
    return 0;

  c->program_number = prog;
  c->pmt_pid = cand->pmt_pid;
  c->pcr_pid = (((unsigned)b[8] & 0x1F) << 8) | b[9];
  pil = tspack_length12(b + 10);
  c->es_count = 0;
  c->audio_count = 0;
  c->ecm_count = 0;
  c->scrambling_mode = 0;
  c->pmt_ca_system_id = 0;
  if (12 + pil <= n) {
    const unsigned char *sd;
    ca = find_desc(b + 12, pil, 0x09, &l);
    if (ca && l >= 4) {
      c->pmt_ca_system_id = ((unsigned)ca[0] << 8) | ca[1];
      add_ecm(c, (((unsigned)ca[2] & 0x1F) << 8) | ca[3]);
    }
    sd = find_desc(b + 12, pil, 0x65, &l);
    if (sd && l >= 1)
      c->scrambling_mode = sd[0];
  }

  end = n - 4;
  i = 12 + pil;
  while (i + 5 <= end && c->es_count < PSI_MAX_ES) {
    psi_es_t *e = &c->es[c->es_count];
    size_t esil = tspack_length12(b + i + 3);
    const unsigned char *desc = b + i + 5;
    if (i + 5 + esil > end)
      break;
    memset(e, 0, sizeof *e);
    e->stream_type = b[i];
    e->pid = (((unsigned)b[i + 1] & 0x1F) << 8) | b[i + 2];
    classify(e, desc, esil);
    ca = find_desc(desc, esil, 0x09, &l);
    if (ca && l >= 4) {
      e->ca_pid = (((unsigned)ca[2] & 0x1F) << 8) | ca[3];
      add_ecm(c, e->ca_pid);
    }
    c->es_count++;
    i += 5 + esil;
  }
  if (c->es_count >= PSI_MAX_ES && i + 5 <= end) {
    if (!c->es_overflow_logged) {
      log_line("psi: PMT for program %u has more than %d ES entries, dropping the rest", prog, PSI_MAX_ES);
      c->es_overflow_logged = 1;
    }
  } else {
    c->es_overflow_logged = 0;
  }
  for (k = 0; k < c->es_count; k++)
    if (c->es[k].cls == PID_AUDIO)
      c->es[k].audio_index = ++c->audio_count;
  c->have_pmt = 1;
  rebuild_class_table(c);
  return 1;
}

/* index into pmt_cand[]/multi[] for program_number, -1 if unknown */
static int find_multi_index(const psi_t *c, unsigned program_number) {
  int k;
  for (k = 0; k < c->pmt_cand_count; k++)
    if (c->pmt_cand[k].program_number == program_number)
      return k;
  return -1;
}

void parse_sdt(psi_t *c) {
  const unsigned char *b = c->sdt.buf;
  size_t n = c->sdt.expect, i, end, dll;

  if (n < 12 || b[0] != 0x42 || crc32_mpeg(b, n) != 0)
    return;
  c->onid = ((unsigned)b[8] << 8) | b[9];
  end = n - 4;
  i = 11;
  while (i + 5 <= end) {
    unsigned sid = ((unsigned)b[i] << 8) | b[i + 1];
    const unsigned char *d = b + i + 5;
    dll = tspack_length12(b + i + 3);
    if (i + 5 + dll > end)
      break;
    if (sid == c->program_number)
      decode_service_desc(d, dll, c->provider_name, c->service_name);
    if (c->multi_mode) {
      int k = find_multi_index(c, sid);
      if (k >= 0)
        decode_service_desc(d, dll, c->multi[k].provider_name, c->multi[k].service_name);
    }
    i += 5 + dll;
  }
  c->have_sdt = 1;
}

void parse_nit(psi_t *c) {
  const unsigned char *b = c->nit.buf;
  size_t n = c->nit.expect, ndl, l;
  const unsigned char *nn;

  if (n < 12 || b[0] != 0x40 || crc32_mpeg(b, n) != 0)
    return;
  ndl = tspack_length12(b + 8);
  if (10 + ndl > n)
    return;
  nn = find_desc(b + 10, ndl, 0x40, &l);
  if (nn)
    copy_name(c->network_name, sizeof c->network_name, nn, l);
  c->have_nit = 1;
}

/* ISO/IEC 13818-1 table 2-30: table_id + 7 more header bytes (same shape as PAT's
   program loop header), then a plain descriptor loop up to the CRC - no program-like
   entries. Takes the first CA_descriptor found (tag 0x09), single-CAS assumption. */
void parse_cat(psi_t *c) {
  const unsigned char *b = c->cat.buf;
  size_t n = c->cat.expect, l;
  const unsigned char *ca;

  if (n < 12 || b[0] != 0x01 || crc32_mpeg(b, n) != 0)
    return;
  c->emm_pid = 0;
  c->ca_system_id = 0;
  ca = find_desc(b + 8, n - 12, 0x09, &l);
  if (ca && l >= 4) {
    c->ca_system_id = ((unsigned)ca[0] << 8) | ca[1];
    c->emm_pid = (((unsigned)ca[2] & 0x1F) << 8) | ca[3];
  }
  c->have_cat = 1;
}
