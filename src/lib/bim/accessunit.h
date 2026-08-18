/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_BIM_ACCESSUNIT_H
#define LIB_BIM_ACCESSUNIT_H

#include "bitreader.h"
#include "bitwriter.h"
#include "lib/tva/bcg_doc.h"
#include "strrepo.h"

/*  TS 102 323 table 56/57 DVBBiMAccessUnit
   accessunit_encode()/accessunit_decode()'s reusable scratch space. zero-init,
   accessunit_scratch_free() when done */
typedef struct {
  void *fuus;
  int fuus_cap;
  void *ptext;
  int ptext_cap;
  bitwriter_t fbw; /* encode's per-fuu buffer, reset() not free()'d between fuus */
} accessunit_scratch_t;

void accessunit_scratch_init(accessunit_scratch_t *sc);
void accessunit_scratch_free(accessunit_scratch_t *sc);

/* TS 102 323 table 56/57 DVBBiMAccessUnit for every channel/programme with non-empty uri. bw/sw must be initialized */
int accessunit_encode(accessunit_scratch_t *sc, const bcg_doc_t *doc, bitwriter_t *bw, strrepo_writer_t *sw, int *out_nfuu);

/* 0 ok, -1 malformed */
int accessunit_decode(accessunit_scratch_t *sc, bitreader_t *br, strrepo_reader_t *sr, bcg_doc_t *doc, int *out_nfuu);

#endif
