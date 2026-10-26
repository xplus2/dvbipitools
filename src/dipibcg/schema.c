/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/bcgip.h"
#include "lib/bim/dvbtva.h"
#include "lib/bim/tvainit.h"
#include "lib/helper/log.h"
#include "schema.h"
#include "version.h"

static int build_init(bcg_schema_t *s, const char *const *more, size_t nmore) {
  bim_tvainit_t ti = {.encoding_version = BIM_TVAINIT_DVB, .char_encoding = 1};
  bim_decoderinit_t di;
  bim_err_t err;
  bitwriter_t bw;
  const unsigned char *p;
  size_t n;
  int rc;
  if (bim_tvainit_decoderinit(s->ns, more, nmore, &di)) return -1;
  bitwriter_init(&bw);
  rc = bim_bcg_tvainit_write(&ti, &di, &bw, &err);
  bim_tvainit_decoderinit_free(&di);
  if (!rc) {
    p = bitwriter_data(&bw, &n);
    unsigned char *m = malloc(n);
    if (m) {
      free(s->init);
      s->init = m;
      memcpy(m, p, n);
      s->init_len = n;
    } else {
      rc = -1;
    }
  }
  bitwriter_free(&bw);
  return rc;
}

int bcg_schema_open(bcg_schema_t *s, const config_t *cfg, const char *ns, int startup) {
  const xsd_compiled_t *c;
  const char *culprit = NULL;
  bim_encode_opts_t eo;
  int rc = -2;
  memset(s, 0, sizeof *s);
  if (!(s->set = xsd_set_new()) || !(s->idx = xsd_index_new())) goto fail;
  rc = xsd_load_for_ns(s->set, s->idx, cfg->schemas, cfg->nschemas, ns, &culprit, s->ns, sizeof s->ns);
  if (rc) goto fail;
  if (!s->ns[0]) snprintf(s->ns, sizeof s->ns, "%s", ns);
  if ((rc = dvbtva_compile(s->set, s->ns, &c))) goto fail;
  memset(&eo, 0, sizeof eo);
  eo.dvb_locator = cfg->dvb_locator;
  eo.dvb_duration = cfg->dvb_duration;
  eo.dvb_pubtime = cfg->dvb_pubtime;
  bcgau_init(&s->au, c, s->ns, &eo);
  if (build_init(s, NULL, 0)) {
    rc = -2;
    goto fail;
  }
  return 0;
fail:
  xsd_load_report(startup ? "GS" : "GE", startup ? "GS" : "GE", rc, culprit, ns, s->set);
  bcg_schema_close(s);
  return -1;
}

int bcg_cast_bind(const bcg_schema_t *s, bim_node_t *root, int startup) {
  const char *bad = NULL;
  if (!dvbtva_cast_bind(s->au.c, s->xc, DVBTVA_SCHEMAS, root, &bad)) return 0;
  log_code(startup ? "GS400319" : "GE400319", "xsi:type %s is not a type of the schemas", bad);
  return -1;
}

static int compile_cb(xsd_set_t *set, const xsd_compiled_t **out, void *arg) { return dvbtva_compile(set, arg, out); }

static int extras_load(bcg_schema_t *s, const config_t *cfg, const char *const *uris, size_t n, int startup, int enc) {
  const dvbtva_extra_codes_t codes = {startup ? "GS" : "GE", startup ? "GS" : "GE", startup ? "GS500101" : "GE500101", "GW000770"};
  const xsd_compiled_t *known[DVBTVA_SCHEMAS] = {NULL};
  if (dvbtva_extras_load(s->xsets, known, uris, n, cfg->schemas, cfg->nschemas, s->idx, enc, &codes, compile_cb, s->ns)) return -1;
  memcpy(s->xc, known, sizeof known);
  dvbtva_schemas_init(&s->tab, s->au.c, n, known);
  s->au.dop.schemas = s->tab.v;
  s->au.dop.nschemas = s->tab.n;
  return 0;
}

int bcg_schema_extras(bcg_schema_t *s, const config_t *cfg, const char *const *uris, size_t n, int startup) {
  if (n < 2) return 0;
  if (n > DVBTVA_SCHEMAS) {
    log_code(startup ? "GS400310" : "GE400310", "bim: DVB-TVA %s", "DecoderInit with too many schemas");
    return -1;
  }
  return extras_load(s, cfg, uris, n, startup, 0);
}

static void euris_free(bcg_schema_t *s) {
  for (size_t k = 1; k < DVBTVA_SCHEMAS; k++) {
    free(s->euris[k]);
    s->euris[k] = NULL;
  }
  s->au.eo.schema_uris = NULL;
  s->au.eo.nschema_uris = 0;
}

int bcg_schema_wire(bcg_schema_t *s, const char *const *uris, size_t n) {
  const xsd_compiled_t *known[DVBTVA_SCHEMAS];
  const char *more[DVBTVA_SCHEMAS];
  size_t m;
  memcpy(known, s->xc, sizeof known);
  memcpy(more, uris, n * sizeof *more);
  m = dvbtva_schemas_pack(known, more, n);
  euris_free(s);
  for (size_t k = 1; k < m; k++)
    if (!(s->euris[k] = strdup(more[k]))) {
      euris_free(s);
      return -1;
    }
  dvbtva_schemas_init(&s->etab, s->au.c, m, known);
  s->au.eo.schemas = m > 1 ? s->etab.v : &s->au.c->schema;
  s->au.eo.nschemas = (int)m;
  s->au.eo.multi = m > 1;
  s->au.eo.schema_uris = (const char *const *)&s->euris[1];
  s->au.eo.nschema_uris = m - 1;
  return build_init(s, s->au.eo.schema_uris, m - 1);
}

int bcg_schema_encode(bcg_schema_t *s, const config_t *cfg, const bim_node_t *root, int startup) {
  const char *uris[DVBTVA_SCHEMAS] = {s->ns};
  int n = xsd_tree_extra_ns(s->au.c, root, uris + 1, DVBTVA_SCHEMAS - 1);
  if (n < 0) {
    log_code(startup ? "GS400310" : "GE400310", "bim: DVB-TVA %s", "document uses too many schemas");
    return -1;
  }
  for (size_t k = 1; k < DVBTVA_SCHEMAS; k++) {
    xsd_set_free(s->xsets[k]);
    s->xsets[k] = NULL;
    s->xc[k] = NULL;
  }
  if (n && extras_load(s, cfg, uris, (size_t)n + 1, startup, 1)) return -1;
  return bcg_schema_wire(s, uris, (size_t)n + 1);
}

void bcg_schema_close(bcg_schema_t *s) {
  euris_free(s);
  for (size_t k = 1; k < DVBTVA_SCHEMAS; k++) xsd_set_free(s->xsets[k]);
  xsd_set_free(s->set);
  xsd_index_free(s->idx);
  free(s->init);
  memset(s, 0, sizeof *s);
}
