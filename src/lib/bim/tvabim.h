/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_TVABIM_H
#define DVBIPITOOLS_LIB_BIM_TVABIM_H

#include <stddef.h>
#include <stdint.h>

#include "bimau.h"
#include "container.h"
#include "fuu.h"
#include "tvafrag.h"

/* TVA profile of BiM, TS 102 822-3-2 clause 4: ISO 15938-1:2002 syntax, absolute context path to the fragment,
   zlib strings, tvadateTime and tvadate codecs */

/* one FragmentUpdateUnit of a fragment, TS 102 822-3-2 4.4.2.2 */
typedef struct {
  unsigned code; /* fragment type, table 7 */
  unsigned command; /* BIM_CMD_ADD, BIM_CMD_REPLACE, BIM_CMD_DELETE */
  uint64_t position; /* Position Code, ignored with PositionCodeFlag 0 */
  bim_node_t *root; /* NULL for delete */
} bim_tvabim_fuu_t;

/* codec bindings by type: dateTime, date. Zlib strings need xsd_compile_opts_t.zlib. n <= BIM_TVABIM_CODECS */
#define BIM_TVABIM_CODECS 2
int bim_tvabim_codecs(xsd_codec_map_t out[BIM_TVABIM_CODECS]);

/* st for c: di (mpegb 0), schemas and opts from o (inflate, zbuf_size, cs). o NULL: defaults */
void bim_tvabim_state(const xsd_compiled_t *c, const bim_decoderinit_t *di, const bim_decode_opts_t *o, bim_fuu_state_t *st);

/* table 56 BiMAccessUnit of n fragments. refs[n]: FragmentUpdateUnit offsets for container, or NULL.
   eo: payload opts (mpegb 0, deflate), schemas default to c. 0 ok, -1 error (err) */
int bim_tvabim_au_write(const xsd_compiled_t *c, const bim_tvans_t *ns, bim_fuu_state_t *st, const bim_encode_opts_t *eo, const bim_tvabim_fuu_t *f, size_t n, bitwriter_t *bw, bim_fragref_t *refs, bim_err_t *err);
/* fragments of types not in table 7 are skipped. *out mallocd, bim_tvabim_au_free. 0 ok, -1 error (err) */
int bim_tvabim_au_parse(const xsd_compiled_t *c, const bim_tvans_t *ns, bim_fuu_state_t *st, const unsigned char *buf, size_t len, bim_tvabim_fuu_t **out, size_t *n, bim_err_t *err);
void bim_tvabim_au_free(bim_tvabim_fuu_t *f, size_t n);

#define BIM_TVABIM_ERR "LE400312"

#endif
