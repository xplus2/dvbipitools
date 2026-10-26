/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_BCGAU_H
#define DVBIPITOOLS_LIB_BIM_BCGAU_H

#include <stdint.h>

#include "bimio.h"
#include "bitwriter.h"
#include "bimreader.h"
#include "container.h"
#include "dvbtva.h"
#include "lib/tva/bcg_doc.h"
#include "strrepo.h"

/* BCG fragments of bcg_doc_t as TVA trees: ProgramInformation per programme, Schedule + ServiceInformation per
   channel with uri, TVAMain from publisher + lang */

enum { BCGAU_PROGRAM, BCGAU_SCHEDULE, BCGAU_SERVICE, BCGAU_TVA_MAIN, BCGAU_OTHER };

typedef struct {
  uint32_t au_off;
  uint32_t str_off;
  uint32_t frag_id;
  uint8_t version;
  uint8_t kind;
  int index; /* programme or channel in the doc */
} bcgau_ref_t;

typedef struct {
  bcgau_ref_t *refs;
  int refs_cap;
} bcgau_scratch_t;

void bcgau_scratch_init(bcgau_scratch_t *sc);
void bcgau_scratch_free(bcgau_scratch_t *sc);

typedef struct {
  const xsd_compiled_t *c;
  bim_tvans_t ns;
  const char *tva_ns;
  bim_encode_opts_t eo;
  bim_decode_opts_t dop;
} bcgau_t;

/* o: dvb_locator, dvb_duration, dvb_pubtime and cs are taken, NULL: none */
void bcgau_init(bcgau_t *a, const xsd_compiled_t *c, const char *tva_ns, const bim_encode_opts_t *o);

/* fragments in emit order, owning their trees */
typedef struct {
  dvbtva_fuu_t *f;
  uint8_t *kind;
  int *index;
  const bcg_channel_t **chan;
  size_t n;
  size_t cap;
} bcgau_frags_t;

/* InstanceDescription/Title per ScheduleEvent */
#define BCGAU_SCHED_TITLES 1u

/* programmes, then schedules, then services of channels ch_from..ch_to-1, ix from bcg_doc_index_build */
int bcgau_frags_range(const bcgau_t *a, const bcg_doc_t *doc, const bcg_doc_index_t *ix, int ch_from, int ch_to, unsigned flags, bcgau_frags_t *p, int *nchan_prog);
/* -1 without publisher */
int bcgau_frags_tva_main(const bcgau_t *a, const bcg_doc_t *doc, bcgau_frags_t *p);
void bcgau_frags_free(bcgau_frags_t *p);

/* access unit of f. chan[i]: channel of a service fragment, adds its tail strings. refs in sc->refs, kind OTHER */
int bcgau_write(bcgau_scratch_t *sc, const bcgau_t *a, const dvbtva_fuu_t *f, size_t n, const bcg_channel_t *const *chan, bitwriter_t *bw, strrepo_writer_t *sw);
int bcgau_frags_write(bcgau_scratch_t *sc, const bcgau_t *a, const bcgau_frags_t *p, bitwriter_t *bw, strrepo_writer_t *sw);
/* TVAMain document of f, consumes the trees of f */
int bcgau_xml(const bcgau_t *a, dvbtva_fuu_t *f, size_t n, char **xml, size_t *len);

int bcgau_encode_range(bcgau_scratch_t *sc, const bcgau_t *a, const bcg_doc_t *doc, const bcg_doc_index_t *ix, int ch_from, int ch_to, unsigned flags,
                       bitwriter_t *bw, strrepo_writer_t *sw, int *out_nfuu, int *out_nchan_prog);
int bcgau_encode_tva_main(bcgau_scratch_t *sc, const bcgau_t *a, const bcg_doc_t *doc, bitwriter_t *bw, strrepo_writer_t *sw);

typedef struct {
  char dtt[64];
  int no_names;
} bcgau_tail_t;

/* fragments of one access unit or document. tails NULL: none */
typedef struct {
  dvbtva_fuu_t *f;
  size_t n;
  bcgau_tail_t *tails;
} bcgau_parsed_t;

/* refs from a container, NULL: strings in order */
int bcgau_parse(const bcgau_t *a, bitreader_t *br, strrepo_reader_t *sr, const bim_fragref_t *refs, int nrefs, bcgau_parsed_t *out);
/* TVA XML text of a TVAMain document */
int bcgau_parse_xml(const bcgau_t *a, const char *xml, size_t len, bcgau_parsed_t *out);
void bcgau_parsed_free(bcgau_parsed_t *p);
/* appends channels, programmes and publisher to doc, schedules need the programme texts of p */
int bcgau_apply(const bcgau_t *a, const bcgau_parsed_t *p, bcg_doc_t *doc);
int bcgau_decode_refs(const bcgau_t *a, bitreader_t *br, strrepo_reader_t *sr, const bim_fragref_t *refs, int nrefs, bcg_doc_t *doc, int *out_nfuu);

const char *bcgau_schedule_end(const bcg_programme_t *programmes, int first, const int *next);

#endif
