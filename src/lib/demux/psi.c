/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "../log.h"
#include "crc32.h"
#include "psi.h"
#include "psi_section_asm.h"
#include "tspack.h"

#define TS_PID_PAT 0x0000
#define TS_PID_CAT 0x0001
#define TS_PID_NIT 0x0010
#define TS_PID_SDT 0x0011

typedef struct {
  unsigned program_number, pmt_pid;
  psi_section_asm_t asm_;
} pmt_cand_t;

struct psi {
  psi_section_asm_t pat, sdt, nit, cat;
  int have_pat, have_pmt, have_sdt, have_nit, have_cat;
  unsigned program_number, pmt_pid, pcr_pid, nit_pid;
  unsigned emm_pid, ca_system_id; /* from CAT's first CA_descriptor, 0 if none */
  unsigned char scrambling_mode; /* from PMT program_info's scrambling_descriptor, 0 if none */
  unsigned tsid, onid;
  psi_es_t es[PSI_MAX_ES];
  int es_count, audio_count;
  unsigned ecm[PSI_MAX_ES];
  int ecm_count;
  char service_name[PSI_NAME], provider_name[PSI_NAME], network_name[PSI_NAME];
  psi_program_t pat_programs[PSI_MAX_PROGRAMS];
  int pat_program_count;
  pmt_cand_t pmt_cand[PSI_MAX_PROGRAMS];
  int pmt_cand_count;
  unsigned preferred_pmt_pid; /* 0 = none, auto-select whichever candidate resolves first */
  int pmt_locked;             /* program_number/pmt_pid finalized, pmt_cand[pmt_lock_idx] is the live one */
  int pmt_lock_idx;

  int multi_mode; /* every candidate resolves independently, see psi_enable_multi_program() */
  psi_multi_program_t multi[PSI_MAX_PROGRAMS];
  int multi_count;

  pid_class_t class_by_pid[8192]; /* direct pid->class, rebuilt on parse_pat/parse_pmt */

  /* overflow logging, edge-triggered: logged once when a repeat first exceeds
     a cap, cleared once a repeat no longer does - avoids spamming every PAT/PMT cycle */
  int pat_program_overflow_logged;
  int pmt_cand_overflow_logged;
  int es_overflow_logged;
};

static void parse_pat(psi_t *c);
static int parse_pmt(psi_t *c, pmt_cand_t *cand);
static void parse_sdt(psi_t *c);
static void parse_nit(psi_t *c);
static void parse_cat(psi_t *c);

/* find descriptor by tag; returns data */
static const unsigned char *find_desc(const unsigned char *d, size_t len, unsigned tag, size_t *dlen) {
  size_t i = 0;
  while (i + 2 <= len) {
    unsigned t = d[i], l = d[i + 1];
    if (i + 2 + l > len)
      break;
    if (t == tag) {
      *dlen = l;
      return d + i + 2;
    }
    i += 2 + l;
  }
  return NULL;
}

/* DVB text: skip charset prefix, controls -> space. not ISO 6937 */
static void copy_name(char *dst, size_t dstsz, const unsigned char *src, size_t len) {
  size_t i = 0, o = 0;
  if (len && src[0] < 0x20) {
    if (src[0] == 0x10 && len >= 3)
      i = 3;
    else if (src[0] == 0x1F && len >= 2)
      i = 2;
    else
      i = 1;
  }
  for (; i < len && o + 1 < dstsz; i++)
    dst[o++] = (src[i] < 0x20) ? ' ' : (char)src[i];
  dst[o] = '\0';
}

static void add_ecm(psi_t *c, unsigned pid) {
  int k;
  if (pid == 0 || pid == 0x1FFF)
    return;
  for (k = 0; k < c->ecm_count; k++)
    if (c->ecm[k] == pid)
      return;
  if (c->ecm_count < PSI_MAX_ES)
    c->ecm[c->ecm_count++] = pid;
}

static void classify(psi_es_t *e, const unsigned char *desc, size_t dlen) {
  size_t l;
  const unsigned char *ld;

  e->codec = CODEC_NONE;
  e->cls = PID_DATA;
  e->lang[0] = '\0';
  switch (e->stream_type) {
    case 0x01:
    case 0x02:
      e->cls = PID_VIDEO;
      e->codec = CODEC_MPEG2V;
      break;
    case 0x1B:
      e->cls = PID_VIDEO;
      e->codec = CODEC_H264;
      break;
    case 0x24:
      e->cls = PID_VIDEO;
      e->codec = CODEC_HEVC;
      break;
    case 0x03:
    case 0x04:
      e->cls = PID_AUDIO;
      e->codec = CODEC_MP2A;
      break;
    case 0x0F:
      e->cls = PID_AUDIO;
      e->codec = CODEC_AAC;
      break;
    case 0x11:
      e->cls = PID_AUDIO;
      e->codec = CODEC_AAC_LATM;
      break;
    case 0x81:
      e->cls = PID_AUDIO;
      e->codec = CODEC_AC3;
      break;
    case 0x87:
      e->cls = PID_AUDIO;
      e->codec = CODEC_EAC3;
      break;
    case 0x06:
      if ((ld = find_desc(desc, dlen, 0x56, &l)) != NULL) {
        size_t x;
        e->cls = PID_TELETEXT;
        /* 5-byte entries; prefer subtitle (type 2/5) */
        for (x = 0; x + 5 <= l; x += 5) {
          int ty = ld[x + 3] >> 3;
          unsigned mag = ld[x + 3] & 0x07;
          unsigned pg = ld[x + 4];
          unsigned page = (mag ? mag : 8) * 100 + ((pg >> 4) & 0x0F) * 10 + (pg & 0x0F);
          if (!e->ttx_page || ty == 2 || ty == 5) {
            e->ttx_page = page;
            e->ttx_type = ty;
            memcpy(e->ttx_lang, ld + x, 3);
            e->ttx_lang[3] = '\0';
          }
          if (ty == 2 || ty == 5)
            break;
        }
      } else if ((ld = find_desc(desc, dlen, 0x59, &l)) != NULL) {
        e->cls = PID_SUBTITLE;
        if (l >= 8) {
          e->sub_type = ld[3];
          e->sub_composition_page = ((unsigned)ld[4] << 8) | ld[5];
          e->sub_ancillary_page = ((unsigned)ld[6] << 8) | ld[7];
          if (!e->lang[0]) {
            memcpy(e->lang, ld, 3);
            e->lang[3] = '\0';
          }
        }
      } else if (find_desc(desc, dlen, 0x6A, &l)) {
        e->cls = PID_AUDIO;
        e->codec = CODEC_AC3;
      } else if (find_desc(desc, dlen, 0x7A, &l)) {
        e->cls = PID_AUDIO;
        e->codec = CODEC_EAC3;
      }
      break;
    case 0x05:
      if (find_desc(desc, dlen, 0x6F, &l))
        e->cls = PID_AIT;
      break;
    default:
      break;
  }

  ld = find_desc(desc, dlen, 0x0A, &l);
  if (ld && l >= 3) {
    e->lang[0] = (char)ld[0];
    e->lang[1] = (char)ld[1];
    e->lang[2] = (char)ld[2];
    e->lang[3] = '\0';
  }
}

/* existing candidate for this pmt_pid, or NULL */
static pmt_cand_t *find_cand(psi_t *c, unsigned pmt_pid) {
  int k;
  for (k = 0; k < c->pmt_cand_count; k++)
    if (c->pmt_cand[k].pmt_pid == pmt_pid)
      return &c->pmt_cand[k];
  return NULL;
}

/* precedence nit>pmt>es>ecm>pcr, stamped lowest-first so higher overwrites (PCR often shares video pid) */
static void rebuild_class_table(psi_t *c) {
  int i;

  memset(c->class_by_pid, 0, sizeof c->class_by_pid); /* PID_UNKNOWN == 0 */
  if (c->have_pmt) {
    c->class_by_pid[c->pcr_pid] = PID_PCR;
    for (i = 0; i < c->ecm_count; i++)
      c->class_by_pid[c->ecm[i]] = PID_ECM;
    for (i = 0; i < c->es_count; i++)
      c->class_by_pid[c->es[i].pid] = c->es[i].cls;
    c->class_by_pid[c->pmt_pid] = PID_PMT;
  } else if (c->have_pat) {
    c->class_by_pid[c->pmt_pid] = PID_PMT;
  }
  if (c->have_pat && c->nit_pid)
    c->class_by_pid[c->nit_pid] = PID_NIT;
}

static void parse_pat(psi_t *c) {
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
static int parse_pmt(psi_t *c, pmt_cand_t *cand) {
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
  if (12 + pil <= n) {
    const unsigned char *sd;
    ca = find_desc(b + 12, pil, 0x09, &l);
    if (ca && l >= 4)
      add_ecm(c, (((unsigned)ca[2] & 0x1F) << 8) | ca[3]);
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

/* service_descriptor (0x48): provider then service name, DVB-text
   length-prefixed. dst untouched if absent/malformed. */
static void decode_service_desc(const unsigned char *d, size_t dll, char *provider_dst, char *service_dst) {
  size_t l, pnl, snl;
  const unsigned char *sd = find_desc(d, dll, 0x48, &l);
  if (!sd || l < 2)
    return;
  pnl = sd[1];
  if (2 + pnl > l)
    return;
  copy_name(provider_dst, PSI_NAME, sd + 2, pnl);
  if (2 + pnl >= l)
    return;
  snl = sd[2 + pnl];
  if (3 + pnl + snl <= l)
    copy_name(service_dst, PSI_NAME, sd + 3 + pnl, snl);
}

/* index into pmt_cand[]/multi[] for program_number, -1 if unknown */
static int find_multi_index(const psi_t *c, unsigned program_number) {
  int k;
  for (k = 0; k < c->pmt_cand_count; k++)
    if (c->pmt_cand[k].program_number == program_number)
      return k;
  return -1;
}

static void parse_sdt(psi_t *c) {
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

static void parse_nit(psi_t *c) {
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
static void parse_cat(psi_t *c) {
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

psi_t *psi_new(void) { return calloc(1, sizeof(psi_t)); }

void psi_free(psi_t *c) { free(c); }

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
    int k;
    for (k = 0; k < c->pmt_cand_count; k++) {
      pmt_cand_t *cand = &c->pmt_cand[k];
      if (cand->pmt_pid != pid)
        continue;
      if (psi_section_asm_feed(&cand->asm_, pl, plen, pusi) && parse_pmt(c, cand)) {
        if (!c->pmt_locked) {
          c->pmt_locked = 1;
          c->pmt_lock_idx = k;
        }
        if (c->multi_mode)
          c->multi[k].resolved = 1;
      }
      break;
    }
  }
}

int psi_wants_pid(const psi_t *c, unsigned pid) {
  int k;

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
  if (c->have_pat)
    for (k = 0; k < c->pmt_cand_count; k++)
      if (c->pmt_cand[k].pmt_pid == pid)
        return 1;
  return 0;
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

const psi_es_t *psi_es(const psi_t *c, int *count) {
  if (count)
    *count = c->es_count;
  return c->es;
}

int psi_audio_count(const psi_t *c) { return c->audio_count; }

const char *psi_service_name(const psi_t *c) { return c->service_name; }
const char *psi_provider_name(const psi_t *c) { return c->provider_name; }
const char *psi_network_name(const psi_t *c) { return c->network_name; }

pid_class_t psi_classify(const psi_t *c, unsigned pid) {
  if (pid == 0x0000)
    return PID_PAT;
  if (pid == 0x0001)
    return PID_CAT;
  if (pid == 0x0010)
    return PID_NIT;
  if (pid == 0x0011)
    return PID_SDT;
  if (pid == 0x0012)
    return PID_EIT;
  if (pid == 0x0013 || pid == 0x0014)
    return PID_OTHER_SI;
  if (pid == 0x1FFF)
    return PID_NULL;
  return c->class_by_pid[pid];
}

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
