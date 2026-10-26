/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_DVBSTREAM_H
#define DVBIPITOOLS_LIB_BIM_DVBSTREAM_H

#include "dvbtva.h"

/* TVAMain document as DVB-TVA stream: DVB-TVA-init with DecoderInit, [4-byte BE length][DVBBiMAccessUnit][string repository] */

typedef enum {
  DVBSTREAM_OK,
  DVBSTREAM_INIT, /* no DVB-TVA-init */
  DVBSTREAM_HEAD, /* DecoderInit invalid or without SchemaURI */
  DVBSTREAM_SHORT, /* no length field */
  DVBSTREAM_LENGTH, /* access unit longer than input, write: above 4 GiB */
  DVBSTREAM_REPO, /* no string repository */
  DVBSTREAM_AU, /* access unit malformed or not encodable */
  DVBSTREAM_ROOT, /* write: document is no TVAMain */
  DVBSTREAM_RULE, /* write: TS 102 323 clause 8 violated */
  DVBSTREAM_OOM
} dvbstream_rc_t;

#define DVBSTREAM_CODES 0x40

#define DVBSTREAM_URI_MAX 256

/* DVB-TVA-init + DecoderInit: SchemaURI[0..nschemas-1], used: bytes */
typedef struct {
  size_t used;
  size_t nschemas;
  char uri[DVBTVA_SCHEMAS][DVBSTREAM_URI_MAX];
} dvbstream_head_t;
/* DVBSTREAM_HEAD: over DVBTVA_SCHEMAS schemas or URI >= DVBSTREAM_URI_MAX bytes */
int dvbstream_head(const unsigned char *p, size_t len, dvbstream_head_t *h, bim_err_t *err);
/* p, len: after the head. *doc mallocd. counts[code]: fragments per type, may be NULL */
int dvbstream_read(const dvbtva_roots_t *r, const bim_decode_opts_t *o, const unsigned char *p, size_t len, bim_node_t **doc, size_t counts[DVBSTREAM_CODES], bim_err_t *err);
/* check: refuse what breaks TS 102 323 clause 8. *out mallocd, sizes: access unit + string repository bytes, set also for DVBSTREAM_LENGTH */
int dvbstream_write(const dvbtva_roots_t *r, const bim_encode_opts_t *eo, const bim_node_t *doc, int check, unsigned char **out, size_t *len, size_t counts[DVBSTREAM_CODES], size_t sizes[2], bim_err_t *err);

/* logs non-zero result. e: tool + code type, "BE". p, len: head+read input, sizes: write output */
void dvbstream_report(const char *e, int writing, int rc, const unsigned char *p, size_t len, const size_t sizes[2], const bim_err_t *err);

#endif
