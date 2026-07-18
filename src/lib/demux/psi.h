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
} psi_es_t;

typedef struct psi psi_t;

psi_t *psi_new(void);
void psi_free(psi_t *c);
void psi_feed(psi_t *c, const unsigned char *pkt); /* one 188-byte packet */

int psi_have_pat(const psi_t *c);
int psi_have_pmt(const psi_t *c);
int psi_have_sdt(const psi_t *c);
int psi_ready(const psi_t *c); /* pat + pmt seen */

unsigned psi_program_number(const psi_t *c);
unsigned psi_pmt_pid(const psi_t *c);
unsigned psi_pcr_pid(const psi_t *c);
unsigned psi_nit_pid(const psi_t *c);

const psi_es_t *psi_es(const psi_t *c, int *count);
int psi_audio_count(const psi_t *c);

const char *psi_service_name(const psi_t *c);
const char *psi_provider_name(const psi_t *c);
const char *psi_network_name(const psi_t *c);

pid_class_t psi_classify(const psi_t *c, unsigned pid);

/* last section incl. CRC, for ts filter edits. NULL until seen */
const unsigned char *psi_pat_section(const psi_t *c, size_t *len);
const unsigned char *psi_pmt_section(const psi_t *c, size_t *len);

const char *pid_class_name(pid_class_t k);
const char *codec_name(codec_t k);

#endif
