/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBIM_FRAGMENT_H
#define DIPIBIM_FRAGMENT_H

#include "bitreader.h"
#include "bitwriter.h"
#include "lib/tva/epg_doc.h"
#include "strrepo.h"

/* TS 102 323 table 57 */
#define DVBCTXPATH_PROGRAM_INFORMATION 0x0001
#define DVBCTXPATH_SCHEDULE 0x0005
#define DVBCTXPATH_SERVICE_INFORMATION 0x0006

/* each below is one fragment instance - a Schedule fragment includes all of
 * one channel's ScheduleEvents (not separately fragmentable), the rest are
 * 1:1 with one epg_doc_t record. 0 ok, -1 oom. */

int fragment_encode_program_information(const epg_programme_t *pr, bitwriter_t *bw, strrepo_writer_t *sw);
/* crid_out is the fragment's own programId, not carried in epg_programme_t */
int fragment_decode_program_information(bitreader_t *br, strrepo_reader_t *sr, char *crid_out, size_t crid_cap, epg_programme_t *pr_out);

int fragment_encode_schedule(const char *channel_id, const epg_programme_t *programmes, int count, bitwriter_t *bw, strrepo_writer_t *sw);
/* called per ScheduleEvent with its Program/@crid to fill title/desc/category. return 0 filled, -1 not found (left empty) */
typedef int (*fragment_text_lookup_fn)(void *ctx, const char *crid, epg_programme_t *pr);
/* appends one epg_programme_t per ScheduleEvent, text fields filled via lookup */
int fragment_decode_schedule(bitreader_t *br, strrepo_reader_t *sr, epg_doc_t *doc, fragment_text_lookup_fn lookup, void *ctx);

int fragment_encode_service_information(const epg_channel_t *c, bitwriter_t *bw, strrepo_writer_t *sw);
int fragment_decode_service_information(bitreader_t *br, strrepo_reader_t *sr, epg_channel_t *c_out);

#endif
