/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_BIM_ACCESSUNIT_H
#define LIB_BIM_ACCESSUNIT_H

#include "bitreader.h"
#include "bitwriter.h"
#include "lib/tva/epg_doc.h"
#include "strrepo.h"

/* TS 102 323 table 56/57 DVBBiMAccessUnit for every channel/programme with non-empty uri. bw/sw must be initialized */
int accessunit_encode(const epg_doc_t *doc, bitwriter_t *bw, strrepo_writer_t *sw, int *out_nfuu);

/* 0 ok, -1 malformed */
int accessunit_decode(bitreader_t *br, strrepo_reader_t *sr, epg_doc_t *doc, int *out_nfuu);

#endif
