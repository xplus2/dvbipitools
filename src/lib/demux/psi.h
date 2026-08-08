/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_DEMUX_PSI_H
#define DIPIREC_DEMUX_PSI_H

#include <stddef.h>

#define PSI_MAX_ES 32
#define PSI_NAME 64

typedef enum {
  PID_UNKNOWN = 0,
  PID_PAT,
  PID_CAT,
  PID_PMT,
  PID_NIT, PID_SDT,
  PID_EIT,
  PID_OTHER_SI,
  PID_NULL,
  PID_PCR,
  PID_VIDEO, PID_AUDIO, PID_TELETEXT, PID_SUBTITLE,
  PID_AIT,
  PID_ECM,
  PID_DATA
} pid_class_t;

typedef enum {
  CODEC_NONE = 0,
  CODEC_MPEG2V,
  CODEC_H264,
  CODEC_HEVC,
  CODEC_MP2A,
  CODEC_AAC,
  CODEC_AAC_LATM,
  CODEC_AC3,
  CODEC_EAC3
} codec_t;

typedef struct {
  unsigned pid;
  unsigned stream_type;
  pid_class_t cls;
  codec_t codec;
  char lang[4];    /* ISO 639, "" if none */
  int audio_index; /* 1-based among audio ES, else 0 */
  unsigned ca_pid; /* ECM pid from ES CA descriptor, 0 if none */
  unsigned ttx_page;   /* teletext page (e.g. 777), 0 if none */
  int ttx_type;        /* teletext_type; 2/5 = subtitle */
  char ttx_lang[4];    /* teletext ISO 639 language */
  unsigned sub_composition_page; /* subtitling_descriptor composition_page_id, 0 if none */
  unsigned sub_ancillary_page;   /* subtitling_descriptor ancillary_page_id, 0 if none */
  unsigned sub_type;             /* subtitling_descriptor subtitling_type, 0 if none */
} psi_es_t;

#define PSI_MAX_PROGRAMS 64

typedef struct {
  unsigned program_number;
  unsigned pmt_pid;
} psi_program_t;

typedef struct {
  unsigned program_number; /* == service_id */
  unsigned pmt_pid;
  int resolved; /* own PMT parsed ok */
  char service_name[PSI_NAME], provider_name[PSI_NAME]; /* "" until own SDT entry seen */
} psi_multi_program_t;

typedef struct psi psi_t;

psi_t *psi_new(void);
void psi_free(psi_t *c);
void psi_feed(psi_t *c, const unsigned char *pkt); /* one 188-byte packet */

/* call any time before the PMT locks in, to force one program's PMT PID
 * instead of auto-selecting whichever PAT-listed candidate resolves first.
 * caller should cross-check against psi_pat_programs() to catch a pid not
 * actually present in the PAT - psi_ready() simply never becomes true. */
void psi_select_pmt_pid(psi_t *c, unsigned pmt_pid);

/* call before feeding: every candidate's PMT resolves independently, no
 * single-first lock. SDT-actual names captured per program too. not
 * enforced mutually exclusive with psi_select_pmt_pid().
 * side effect: single-current-program accessors (psi_program_number,
 * psi_pmt_pid, psi_pcr_pid, psi_es, psi_audio_count, psi_scrambling_mode,
 * psi_service_name, psi_provider_name) then track whichever program most
 * recently resolved, not a stable one - use psi_multi_programs() instead. */
void psi_enable_multi_program(psi_t *c);

/* one entry per PAT-listed program, valid once psi_have_pat().
 * empty unless psi_enable_multi_program() was called. */
const psi_multi_program_t *psi_multi_programs(const psi_t *c, int *count);

int psi_have_pat(const psi_t *c);
int psi_have_pmt(const psi_t *c);
int psi_have_sdt(const psi_t *c);
int psi_have_cat(const psi_t *c);
int psi_ready(const psi_t *c); /* pat + pmt seen */

/* every program the PAT listed (network_pid entry excluded), valid once psi_have_pat() */
const psi_program_t *psi_pat_programs(const psi_t *c, int *count);

unsigned psi_program_number(const psi_t *c);
unsigned psi_pmt_pid(const psi_t *c);
unsigned psi_pcr_pid(const psi_t *c);
unsigned psi_nit_pid(const psi_t *c);
unsigned psi_transport_stream_id(const psi_t *c);
unsigned psi_original_network_id(const psi_t *c);

/* CAT's first CA_descriptor (single-CAS assumption): EMM pid / ca_system_id, 0 if none
   seen yet or the CAT carried no CA_descriptor */
unsigned psi_emm_pid(const psi_t *c);
unsigned psi_ca_system_id(const psi_t *c);

/* PMT program_info's scrambling_descriptor (ETSI TS 103 127 clause 7) mode byte,
   0 if no PMT seen yet or it carried none. Common values: 0x02 CSA2, 0x10 CISSA.
   this header doesn't define those, they're not PSI/SI, just where the byte lives */
unsigned char psi_scrambling_mode(const psi_t *c);

const psi_es_t *psi_es(const psi_t *c, int *count);
int psi_audio_count(const psi_t *c);

const char *psi_service_name(const psi_t *c);
const char *psi_provider_name(const psi_t *c);
const char *psi_network_name(const psi_t *c);

pid_class_t psi_classify(const psi_t *c, unsigned pid);

/* 1 if psi_feed() would act on this pid (table pid, locked pmt, or a pmt candidate), 0: no-op */
int psi_wants_pid(const psi_t *c, unsigned pid);

/* last section incl. CRC, for ts filter edits. NULL until seen */
const unsigned char *psi_pat_section(const psi_t *c, size_t *len);
const unsigned char *psi_pmt_section(const psi_t *c, size_t *len);

const char *pid_class_name(pid_class_t k);
const char *codec_name(codec_t k);

#endif
