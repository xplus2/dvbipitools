/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/dvbstream.h"
#include "lib/bim/tvainit.h"
#include "lib/helper/beutil.h"
#include "lib/helper/log.h"

int dvbstream_head(const unsigned char *p, size_t len, dvbstream_head_t *h, bim_err_t *err) {
  bim_tvainit_t ti;
  bim_decoderinit_t di;
  size_t ptr;
  size_t n;
  int bad;
  if (bim_tvainit_parse(p, len, &ti, &ptr, err) || ti.encoding_version != BIM_TVAINIT_DVB) return DVBSTREAM_INIT;
  if (bim_decoderinit_parse(p + ptr, len - ptr, 0, NULL, NULL, &di, &n, err)) return DVBSTREAM_HEAD;
  bad = !di.nschemas || di.nschemas > DVBTVA_SCHEMAS;
  for (size_t k = 0; !bad && k < di.nschemas; k++) bad = !di.schemas[k].uri || strlen(di.schemas[k].uri) >= DVBSTREAM_URI_MAX;
  if (bad) {
    bim_decoderinit_free(&di);
    bim_fail(err, BIM_DVBTVA_ERR, "bim: DVB-TVA %s", "DecoderInit without usable SchemaURI");
    return DVBSTREAM_HEAD;
  }
  h->nschemas = di.nschemas;
  for (size_t k = 0; k < di.nschemas; k++) memcpy(h->uri[k], di.schemas[k].uri, strlen(di.schemas[k].uri) + 1);
  h->used = ptr + n;
  bim_decoderinit_free(&di);
  return DVBSTREAM_OK;
}

static void count(size_t *counts, unsigned code) {
  if (counts && code < DVBSTREAM_CODES) counts[code]++;
}

int dvbstream_read(const dvbtva_roots_t *r, const bim_decode_opts_t *o, const unsigned char *p, size_t len, bim_node_t **doc, size_t counts[DVBSTREAM_CODES], bim_err_t *err) {
  bitreader_t br;
  strrepo_reader_t sr;
  dvbtva_fuu_t *f = NULL;
  size_t n = 0;
  size_t al;
  int rc = DVBSTREAM_AU;
  if (len < 4) return DVBSTREAM_SHORT;
  al = be32_get(p);
  if (al > len - 4) return DVBSTREAM_LENGTH;
  bitreader_init(&br, p + 4, al);
  if (strrepo_reader_init(&sr, p + 4 + al, len - 4 - al)) return DVBSTREAM_REPO;
  if (dvbtva_au_parse(dvbtva_tva_root, (void *)r, o, &br, &sr, NULL, 0, &f, &n, err)) return DVBSTREAM_AU;
  for (size_t i = 0; i < n; i++) count(counts, f[i].code);
  if (bim_tvafrag_join(r->c, r->ns, f, n, doc)) bim_fail(err, BIM_DVBTVA_ERR, "bim: DVB-TVA %s", "fragments not joinable");
  else rc = DVBSTREAM_OK;
  dvbtva_au_free(f, n);
  return rc;
}

int dvbstream_write(const dvbtva_roots_t *r, const bim_encode_opts_t *eo, const bim_node_t *doc, int check, unsigned char **out, size_t *len, size_t counts[DVBSTREAM_CODES], size_t sizes[2], bim_err_t *err) {
  bim_tvainit_t ti = {.encoding_version = BIM_TVAINIT_DVB, .char_encoding = 1};
  bim_decoderinit_t di;
  bim_tvapart_t *parts = NULL;
  bitwriter_t head;
  bitwriter_t au;
  strrepo_writer_t sw;
  const unsigned char *h, *a, *s;
  size_t n = 0, hl, al, sl;
  int rc = DVBSTREAM_AU;
  int sp = bim_tvafrag_split(r->c, r->ns, doc, &parts, &n);
  if (sp) {
    return sp == -2 ? DVBSTREAM_ROOT : (bim_oom(err), DVBSTREAM_OOM);
  }
  for (size_t i = 0; check && i < n; i++)
    if (dvbtva_check_fragment(r->ns, &parts[i], err)) goto rule;
  if (check && dvbtva_check_set(r->ns, parts, n, err)) goto rule;
  bitwriter_init(&head);
  bitwriter_init(&au);
  if (strrepo_writer_init(&sw)) {
    bim_tvafrag_free(parts, n);
    return bim_oom(err), DVBSTREAM_OOM;
  }
  if (bim_tvainit_decoderinit(r->ns->tva, eo->schema_uris, eo->nschema_uris, &di)) {
    bim_oom(err);
    rc = DVBSTREAM_OOM;
    goto out;
  }
  if (bim_tvainit_write(&ti, &head, err) || bim_decoderinit_write(&di, NULL, NULL, &head, err)) goto di;
  if (dvbtva_au_write(dvbtva_tva_root, (void *)r, eo, parts, n, &au, &sw, NULL, err)) goto di;
  h = bitwriter_data(&head, &hl);
  a = bitwriter_data(&au, &al);
  s = strrepo_writer_data(&sw, &sl);
  sizes[0] = al;
  sizes[1] = sl;
  if (al > UINT32_MAX || sl > UINT32_MAX) {
    rc = DVBSTREAM_LENGTH;
    goto di;
  }
  if (!(*out = malloc(hl + 4 + al + sl))) {
    bim_oom(err);
    rc = DVBSTREAM_OOM;
    goto di;
  }
  memcpy(*out, h, hl);
  be32_put(*out + hl, (uint32_t)al);
  memcpy(*out + hl + 4, a, al);
  memcpy(*out + hl + 4 + al, s, sl);
  *len = hl + 4 + al + sl;
  for (size_t i = 0; i < n; i++) count(counts, parts[i].code);
  rc = DVBSTREAM_OK;
di:
  bim_tvainit_decoderinit_free(&di);
out:
  bim_tvafrag_free(parts, n);
  strrepo_writer_free(&sw);
  bitwriter_free(&au);
  bitwriter_free(&head);
  return rc;
rule:
  bim_tvafrag_free(parts, n);
  return DVBSTREAM_RULE;
}

void dvbstream_report(const char *e, int writing, int rc, const unsigned char *p, size_t len, const size_t sizes[2], const bim_err_t *err) {
  char code[16];
  const char *fmt;
  unsigned num;
  switch (rc) {
    case DVBSTREAM_INIT: num = 400242; fmt = "missing or unsupported DVB-TVA-init"; break;
    case DVBSTREAM_SHORT: num = 400103; fmt = "input truncated: %zu bytes, need 4 byte length header"; break;
    case DVBSTREAM_LENGTH: num = writing ? 400108 : 400104; fmt = writing ? "encoded size exceeds 4 GiB format limit: %zu+%zu bytes" : "input truncated: bitstream needs %zu bytes, %zu present"; break;
    case DVBSTREAM_REPO: num = 400105; fmt = "string repository missing or unsupported encoding"; break;
    case DVBSTREAM_OOM: num = 500101; fmt = "out of memory"; break;
    case DVBSTREAM_RULE: num = 400318; fmt = "DVB-TVA content rule violated: %s"; break;
    default: num = writing ? 400315 : 400316; fmt = writing ? "BiM encoding failed: %s" : "BiM decoding failed: %s"; break;
  }
  snprintf(code, sizeof code, "%s%06u", e, num);
  if (rc == DVBSTREAM_LENGTH) {
    if (writing) log_code(code, fmt, sizes[0], sizes[1]);
    else if (p) log_code(code, fmt, (size_t)be32_get(p), len - 4);
  }
  else if (rc == DVBSTREAM_SHORT) log_code(code, fmt, len);
  else if (rc == DVBSTREAM_INIT || rc == DVBSTREAM_REPO || rc == DVBSTREAM_OOM) log_code(code, "%s", fmt);
  else log_code(code, fmt, err->msg);
}
