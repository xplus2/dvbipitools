/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_ACCESSUNIT_H
#define DVBIPITOOLS_LIB_BIM_ACCESSUNIT_H

#include <stdint.h>

#include "bimreader.h"
#include "bitwriter.h"
#include "lib/tva/bcg_doc.h"
#include "strrepo.h"

typedef struct {
  uint32_t au_off;
  uint32_t str_off;
} accessunit_ref_t;

/*  TS 102 323 table 56/57 DVBBiMAccessUnit
   accessunit_encode()/accessunit_decode()'s reusable scratch space. zero-init,
   accessunit_scratch_free() when done */
typedef struct {
  void *fuus;
  int fuus_cap;
  void *ptext;
  int ptext_cap;
  accessunit_ref_t *refs; /* encode output, one per fuu in emit order */
  int refs_cap;
  bitwriter_t fbw; /* encode's per-fuu buffer, reset() not free()'d between fuus */
} accessunit_scratch_t;

void accessunit_scratch_init(accessunit_scratch_t *sc);
void accessunit_scratch_free(accessunit_scratch_t *sc);

/* TS 102 323 table 56/57 DVBBiMAccessUnit for every channel/programme with non-empty uri. bw/sw must be initialized */
int accessunit_encode(accessunit_scratch_t *sc, const bcg_doc_t *doc, bitwriter_t *bw, strrepo_writer_t *sw, int *out_nfuu);

/* programme-to-channel map of one doc, valid while doc is unchanged. zero-init not needed, accessunit_index_free() when done */
typedef struct {
  int *nprog; /* per channel */
  int *first; /* per channel, -1 none */
  int *next;  /* per programme, next of same channel, -1 ends */
} accessunit_index_t;

/* 0 ok, -1 oom */
int accessunit_index_build(accessunit_index_t *ix, const bcg_doc_t *doc);
void accessunit_index_free(accessunit_index_t *ix);

/* ix from accessunit_index_build(doc). programmes are emitted grouped by channel */
int accessunit_encode_range(accessunit_scratch_t *sc, const bcg_doc_t *doc, const accessunit_index_t *ix, int ch_from, int ch_to, bitwriter_t *bw, strrepo_writer_t *sw, int *out_nfuu, int *out_nchan_prog);

/* 0 ok, -1 malformed */
int accessunit_decode(accessunit_scratch_t *sc, bitreader_t *br, strrepo_reader_t *sr, bcg_doc_t *doc, int *out_nfuu);

/* a fuu found in refs by au_off reads strings from its str_off, others continue sequentially. 0 ok, -1 malformed */
int accessunit_decode_refs(accessunit_scratch_t *sc, bitreader_t *br, strrepo_reader_t *sr, const accessunit_ref_t *refs, int nrefs, bcg_doc_t *doc, int *out_nfuu);

#endif
