/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/dvbtva.h"
#include "lib/bim/sdnsbim.h"
#include "lib/bim/tvabim.h"
#include "lib/bim/tvainit.h"
#include "lib/helper/log.h"
#include "job.h"
#include "version.h"

int job_mpegb(const config_t *cfg) { return cfg->profile != PROFILE_GENERIC || cfg->syntax == SYNTAX_23001; }

int job_uses_zlib(const config_t *cfg) { return cfg->profile == PROFILE_TVA || cfg->profile == PROFILE_SDNS; }

static int xsd_failed(xsd_set_t *s) {
  log_code(xsd_set_error_code(s), "%s", xsd_set_error(s));
  return -1;
}

int job_schema_load(job_t *j, const char *ns) {
  const char *culprit;
  int rc;
  if (!(j->set = xsd_set_new()) || !(j->idx = xsd_index_new())) {
    log_code("BE500101", "out of memory");
    return -1;
  }
  rc = xsd_load_for_ns(j->set, j->idx, j->cfg->schemas, j->cfg->nschemas, ns, &culprit, NULL, 0);
  if (rc) xsd_load_report("BS", "BE", rc, culprit, ns, j->set);
  return rc ? -1 : 0;
}

static void job_options(job_t *j) {
  const config_t *cfg = j->cfg;
  j->eo.schemas = &j->c->schema;
  j->eo.nschemas = 1;
  j->eo.lax_schema = -1;
  j->eo.frag_format = 1;
  j->eo.deflate = Z_DEFLATE;
  j->eo.zbuf_size = cfg->zbuf_size;
  j->eo.zgather = cfg->zgather;
  j->eo.compat = j->dop.compat = cfg->compat;
  j->eo.diag = j->diag;
  j->eo.ndiag = sizeof j->diag;
  if (cfg->profile == PROFILE_GENERIC) {
    j->eo.len_mode = (int)cfg->len_mode;
    j->eo.deferred = cfg->deferred;
    j->eo.cast = cfg->cast;
  }
  j->eo.mpegb = j->dop.mpegb = cfg->profile == PROFILE_SDNS; /* TS 102 034 5.5: ISO/IEC 23001-1 */
  if (cfg->profile == PROFILE_DVB_TVA) {
    j->eo.dvb_locator = cfg->dvb_locator;
    j->eo.dvb_duration = cfg->dvb_duration;
    j->eo.dvb_pubtime = cfg->dvb_pubtime;
  }
  j->dop.schemas = &j->c->schema;
  j->dop.nschemas = 1;
  j->dop.lax_schema = -1;
  j->dop.inflate = Z_INFLATE;
}

static int compile_set(const job_t *j, xsd_set_t *set, const xsd_compiled_t **c) {
  xsd_codec_map_t codecs[DVBTVA_CODECS];
  xsd_compile_opts_t o = {0};
  switch (j->cfg->profile) {
    case PROFILE_DVB_TVA:
      break;
    case PROFILE_TVA:
      o.ncodecs = bim_tvabim_codecs(codecs);
      o.codecs = codecs;
      o.zlib = 1;
      break;
    case PROFILE_SDNS:
      o.zlib = 1;
      o.mpegb = 1;
      break;
    default:
      o.mpegb = job_mpegb(j->cfg);
      o.compat = j->cfg->compat;
      break;
  }
  return j->cfg->profile == PROFILE_DVB_TVA ? dvbtva_compile(set, j->ns.tva, c) : xsd_compile(set, &o, c);
}

int job_schema_compile(job_t *j) {
  if (compile_set(j, j->set, &j->c)) return xsd_failed(j->set);
  bim_tvans_detect(j->c, &j->ns);
  job_options(j);
  return 0;
}

static int compile_cb(xsd_set_t *set, const xsd_compiled_t **out, void *arg) { return compile_set(arg, set, out); }

/* enc: namespace without file stays wildcard of main schema, unlisted */
static int extras_load(job_t *j, int enc) {
  static const dvbtva_extra_codes_t codes = {"BS", "BE", "BE500101", "BW000770"};
  const xsd_compiled_t *known[DVBTVA_SCHEMAS] = {NULL};
  if (dvbtva_extras_load(j->xsets, known, j->uris, j->nschemas, j->cfg->schemas, j->cfg->nschemas, j->idx, enc, &codes, compile_cb, j)) return -1;
  memcpy(j->xc, known, sizeof known);
  return 0;
}

int job_schemas_open(job_t *j) {
  if (j->nschemas > DVBTVA_SCHEMAS) {
    log_code("BE400310", "bim: DVB-TVA %s", "DecoderInit with too many schemas");
    return -1;
  }
  if (j->nschemas < 2) return 0;
  if (extras_load(j, 0)) return -1;
  dvbtva_schemas_init(&j->sch, j->c, j->nschemas, j->xc);
  j->dop.schemas = j->sch.v;
  j->dop.nschemas = j->sch.n;
  return 0;
}

int job_schemas_encode(job_t *j, const bim_node_t *root) {
  const char *ns[DVBTVA_SCHEMAS];
  int n = xsd_tree_extra_ns(j->c, root, ns, DVBTVA_SCHEMAS - 1);
  size_t m;
  if (n < 0) {
    log_code("BE400310", "bim: DVB-TVA %s", "document uses too many schemas");
    return -1;
  }
  if (!n) return 0;
  j->nschemas = (size_t)n + 1;
  for (int k = 0; k < n; k++) j->uris[k + 1] = ns[k];
  if (extras_load(j, 1)) return -1;
  m = dvbtva_schemas_pack(j->xc, j->uris, j->nschemas);
  if (m < 2) {
    j->nschemas = 0;
    return 0;
  }
  j->nschemas = m;
  dvbtva_schemas_init(&j->sch, j->c, m, j->xc);
  j->eo.schemas = j->dop.schemas = j->sch.v;
  j->eo.nschemas = j->dop.nschemas = j->sch.n;
  j->eo.multi = 1;
  j->eo.schema_uris = &j->uris[1];
  j->eo.nschema_uris = m - 1;
  return 0;
}

int job_cast_bind(const job_t *j, bim_node_t *root) {
  const char *bad = NULL;
  if (!dvbtva_cast_bind(j->c, j->xc, j->nschemas, root, &bad)) return 0;
  log_code("BE400319", "xsi:type %s is not a type of the schemas", bad);
  return -1;
}

void job_free(job_t *j) {
  bim_fuu_state_close(&j->st);
  if (j->di_parsed) bim_decoderinit_free(&j->di);
  else if (j->cfg->profile == PROFILE_SDNS) bim_sdnsbim_decoderinit_free(&j->di);
  else bim_tvainit_decoderinit_free(&j->di);
  for (size_t k = 1; k < DVBTVA_SCHEMAS; k++) xsd_set_free(j->xsets[k]);
  xsd_set_free(j->set);
  xsd_index_free(j->idx);
  xml_doc_free(j->xml);
  free(j->buf);
}

void job_count_fragment(job_t *j, unsigned code) {
  if (code < FRAG_CODES) j->nfrag[code]++;
}

void job_log_fragments(const job_t *j) {
  if (!j->cfg->verbose) return;
  for (unsigned code = 0; code < FRAG_CODES; code++) {
    const char *name = j->nfrag[code] ? bim_tvafrag_name(code) : NULL;
    if (name) log_code("BD700172", "%s: %zu", name, j->nfrag[code]);
  }
}
