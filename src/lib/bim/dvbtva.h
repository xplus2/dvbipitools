/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_DVBTVA_H
#define DVBIPITOOLS_LIB_BIM_DVBTVA_H

#include <stddef.h>
#include <stdint.h>

#include "bimio.h"
#include "container.h"
#include "codec.h"
#include "decoderinit.h"
#include "strrepo.h"
#include "tvafrag.h"

/* DVB profile of TVA BiM, TS 102 323 9.4 */

/* root element of a DVBContextPath value, 0 found, -1 unknown */
typedef int (*dvbtva_root_fn)(void *ctx, unsigned code, bim_tvaroot_t *out);

/* TVA fragment types of table 58 in c */
typedef struct {
  const xsd_compiled_t *c;
  const bim_tvans_t *ns;
} dvbtva_roots_t;
int dvbtva_tva_root(void *ctx, unsigned code, bim_tvaroot_t *out);

/* fragment of context path code, owns root */
typedef bim_tvapart_t dvbtva_fuu_t;

/* table 57 DVBBiMAccessUnit of n fragments. eo: payload options (mpegb 0), sw initialized. refs[n] or NULL.
   0 ok, -1 error (err) */
int dvbtva_au_write(dvbtva_root_fn root, void *rctx, const bim_encode_opts_t *eo, const dvbtva_fuu_t *f, size_t n, bitwriter_t *bw, strrepo_writer_t *sw,
                    bim_fragref_t *refs, bim_err_t *err);
/* refs: from a container, NULL: strings read in order. fragments of reserved or user defined paths are skipped,
   later fragment reading strings without ref after skip fails.
   *out mallocd, dvbtva_au_free. 0 ok, -1 error (err) */
int dvbtva_au_parse(dvbtva_root_fn root, void *rctx, const bim_decode_opts_t *o, bitreader_t *br, strrepo_reader_t *sr, const bim_fragref_t *refs, size_t nrefs,
                    dvbtva_fuu_t **out, size_t *n, bim_err_t *err);
void dvbtva_au_free(dvbtva_fuu_t *f, size_t n);

/* extra strings after payload: called after payload i of f is written (k: kept fragment index on read).
   Non-zero fails unit */
typedef int (*dvbtva_wpost_fn)(void *ctx, size_t i, strrepo_writer_t *sw);
typedef int (*dvbtva_rpost_fn)(void *ctx, size_t k, const dvbtva_fuu_t *f, strrepo_reader_t *sr);
int dvbtva_au_write_ex(dvbtva_root_fn root, void *rctx, const bim_encode_opts_t *eo, const dvbtva_fuu_t *f, size_t n, bitwriter_t *bw, strrepo_writer_t *sw,
                       bim_fragref_t *refs, dvbtva_wpost_fn post, void *pctx, bim_err_t *err);
int dvbtva_au_parse_ex(dvbtva_root_fn root, void *rctx, const bim_decode_opts_t *o, bitreader_t *br, strrepo_reader_t *sr, const bim_fragref_t *refs, size_t nrefs,
                       dvbtva_rpost_fn post, void *pctx, dvbtva_fuu_t **out, size_t *n, bim_err_t *err);

/* schemas of a DecoderInit, SchemaID order */
#define DVBTVA_SCHEMAS 16
typedef struct {
  bim_schema_t v[DVBTVA_SCHEMAS];
  int n;
} dvbtva_schemas_t;
/* v[0] is c, v[k] known[k], unknown if NULL. Unknown chunks skipped, TS 102 323 9.4.4.1. -1 n outside 1..DVBTVA_SCHEMAS */
int dvbtva_schemas_init(dvbtva_schemas_t *s, const xsd_compiled_t *c, size_t n, const xsd_compiled_t *const *known);
typedef int (*dvbtva_extra_compile_t)(xsd_set_t *set, const xsd_compiled_t **out, void *arg);
/* start, run: xsd_load_report types. oom, warn: codes of out of memory and of a schema not found */
typedef struct {
  const char *start;
  const char *run;
  const char *oom;
  const char *warn;
} dvbtva_extra_codes_t;
/* uris[1..n-1] loaded from paths into sets[k], compiled into known[k]. Not found: known[k] stays NULL, warn unless enc.
   x: schema file index, NULL private. 0 ok, -1 error (logged) */
int dvbtva_extras_load(xsd_set_t **sets, const xsd_compiled_t **known, const char *const *uris, size_t n, const char *const *paths, int npaths, xsd_index_t *x,
                       int enc, const dvbtva_extra_codes_t *codes, dvbtva_extra_compile_t fn, void *arg);
/* known[1..n-1], uris[1..n-1] in place: only entries with known set. Returns new n */
size_t dvbtva_schemas_pack(const xsd_compiled_t **known, const char **uris, size_t n);
/* xsi:type of root bound in c and known[1..n-1] (NULL: skipped). 0 ok, -1 unknown type (*bad: QName) */
int dvbtva_cast_bind(const xsd_compiled_t *c, const xsd_compiled_t *const *known, size_t n, bim_node_t *root, const char **bad);

/* xsd_compile of s with DVB codecs for tva_ns */
int dvbtva_compile(xsd_set_t *s, const char *tva_ns, const xsd_compiled_t **c);

/* TS 102 323 9.4.3 default codecs by type, n <= DVBTVA_CODECS. ns->tva NULL: no termReferenceType */
#define DVBTVA_CODECS 5
int dvbtva_codecs(const bim_tvans_t *ns, xsd_codec_map_t out[DVBTVA_CODECS]);

/* TS 102 323 clause 8 content rules. First violation in err, 0 conformant, -1 violated.
   fragment: 8.3 .. 8.9. set: 8.2 references between fragments, 8.5 Schedule sequence per service */
int dvbtva_check_fragment(const bim_tvans_t *ns, const dvbtva_fuu_t *f, bim_err_t *err);
int dvbtva_check_set(const bim_tvans_t *ns, const dvbtva_fuu_t *f, size_t n, bim_err_t *err);

#define BIM_DVBTVA_ERR "LE400310"
#define BIM_DVBTVA_RULE "LE400311"

#endif
