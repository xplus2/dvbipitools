/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_BCGFRAG_H
#define DVBIPITOOLS_LIB_BIM_BCGFRAG_H

#include "bcgau.h"

/* one fragment instance in a. Schedule: all ScheduleEvents of one channel, others 1:1 with a bcg_doc_t record.
   Trees bound to schema namespaces. NULL on oom or not in schema */

bim_node_t *bcgfrag_program(const bcgau_t *a, const bcg_programme_t *pr);
/* chain: first index (-1 none), next[j] follows j (-1 ends), any order. titles: InstanceDescription/Title per event */
bim_node_t *bcgfrag_schedule(const bcgau_t *a, const char *channel_id, const bcg_programme_t *programmes, int first, const int *next, int titles);
bim_node_t *bcgfrag_service(const bcgau_t *a, const bcg_channel_t *c);
/* TS 102 323 table 55 skeleton, tables of the schema only. publisher, lang NULL: absent */
bim_node_t *bcgfrag_tva_main(const bcgau_t *a, const char *publisher, const char *lang);

/* crid_out: fragment's own programId, not carried in bcg_programme_t. on success caller frees pr_out text (bcg_programme_free_text) */
int bcgfrag_read_program(const bim_node_t *root, char *crid_out, size_t crid_cap, bcg_programme_t *pr_out);
/* called per ScheduleEvent with its Program/@crid to fill title/desc/category. return 0 filled, -1 not found (left empty), -2 OOM (aborts) */
typedef int (*bcgfrag_text_lookup_fn)(void *ctx, const char *crid, bcg_programme_t *pr);
/* appends one bcg_programme_t per ScheduleEvent, text fields filled via lookup */
int bcgfrag_read_schedule(const bim_node_t *root, bcg_doc_t *doc, bcgfrag_text_lookup_fn lookup, void *ctx);
/* dtt: "dvb://" triplet of the channel or "", no_names: Name was synthesized from the id */
int bcgfrag_read_service(const bim_node_t *root, const char *dtt, int no_names, bcg_channel_t *c_out);
/* publisher, lang into doc, "" if absent. -1 too long */
int bcgfrag_read_tva_main(const bim_node_t *root, bcg_doc_t *doc);

#endif
