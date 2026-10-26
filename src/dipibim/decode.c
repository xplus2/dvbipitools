/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/beutil.h"
#include "lib/bim/bimau.h"
#include "lib/bim/dvbstream.h"
#include "lib/bim/dvbtva.h"
#include "lib/bim/nodexml.h"
#include "lib/bim/sdnsbim.h"
#include "lib/bim/tvabim.h"
#include "lib/bim/tvainit.h"
#include "lib/helper/log.h"
#include "lib/sys/ioutil.h"
#include "job.h"
#include "version.h"

static int decode_failed(job_t *j) {
  log_code("BE400316", "BiM decoding failed: %s", j->err.msg);
  return -1;
}

static int write_xml(job_t *j, const bim_node_t *root, FILE *out) {
  bim_nsdecl_t pre[2];
  bim_xml_opts_t xo = {.prefixes = pre, .nprefixes = 0, .indent = 2, .declaration = 1};
  char *text;
  size_t len;
  int rc;
  if (j->ns.tva2) pre[xo.nprefixes++] = (bim_nsdecl_t){"tva2", j->ns.tva2};
  if (j->ns.intr) pre[xo.nprefixes++] = (bim_nsdecl_t){"int", j->ns.intr};
  if (bim_node_to_xml(root, &xo, &text, &len)) {
    log_code("BE500101", "out of memory");
    return -1;
  }
  rc = job_put_all(out, (const unsigned char *)text, len);
  free(text);
  return rc;
}

static int decode_dvbtva(job_t *j, const unsigned char *p, size_t len, FILE *out) {
  dvbtva_roots_t r;
  bim_node_t *doc = NULL;
  size_t used = 0;
  size_t n = 0;
  int rc = dvbstream_head(p, len, &j->head, &j->err);
  if (rc) {
    dvbstream_report("BE", 0, rc, p, len, NULL, &j->err);
    return -1;
  }
  used = j->head.used;
  j->nschemas = j->head.nschemas;
  for (size_t k = 0; k < j->nschemas; k++) j->uris[k] = j->head.uri[k];
  j->root_ns = j->ns.tva = j->uris[0];
  if (job_schema_load(j, j->root_ns) || job_schema_compile(j) || job_schemas_open(j)) return -1;
  r = (dvbtva_roots_t){j->c, &j->ns};
  p += used;
  len -= used;
  if ((rc = dvbstream_read(&r, &j->dop, p, len, &doc, j->nfrag, &j->err))) {
    dvbstream_report("BE", 0, rc, p, len, NULL, &j->err);
    return -1;
  }
  rc = write_xml(j, doc, out);
  bim_node_free(doc);
  if (rc || !j->cfg->verbose) return rc;
  for (size_t i = 0; i < FRAG_CODES; i++) n += j->nfrag[i];
  log_code("BD700171", "%zu fragments read", n);
  job_log_fragments(j);
  return 0;
}

static int decode_sdns(job_t *j, const unsigned char *p, size_t len, FILE *out) {
  bim_sdnsroots_t sr = {j->c, j->root_ns};
  dvbtva_fuu_t *f = NULL;
  bitreader_t br;
  strrepo_reader_t srd;
  size_t al, n = 0;
  int rc = -1;
  if (len < 4) {
    log_code("BE400103", "input truncated: %zu bytes, need 4 byte length header", len);
    return -1;
  }
  al = be32_get(p);
  if (al > len - 4) {
    log_code("BE400104", "input truncated: bitstream needs %zu bytes, %zu present", al, len - 4);
    return -1;
  }
  bitreader_init(&br, p + 4, al);
  if (strrepo_reader_init(&srd, p + 4 + al, len - 4 - al)) {
    log_code("BE400105", "string repository missing or unsupported encoding");
    return -1;
  }
  if (dvbtva_au_parse(bim_sdnsbim_root, &sr, &j->dop, &br, &srd, NULL, 0, &f, &n, &j->err)) return decode_failed(j);
  if (n != 1) {
    bim_fail(&j->err, BIM_DVBTVA_ERR, "bim: DVB-TVA %s", "segment is not one fragment");
    decode_failed(j);
  } else {
    rc = write_xml(j, f[0].root, out);
  }
  if (!rc && j->cfg->verbose) log_code("BD700171", "%zu fragments read", n);
  dvbtva_au_free(f, n);
  return rc;
}

static int decode_tva(job_t *j, const unsigned char *p, size_t len, FILE *out) {
  bim_tvabim_fuu_t *f = NULL;
  bim_tvapart_t *parts = NULL;
  bim_node_t *doc = NULL;
  size_t n = 0, k = 0;
  int rc = -1;
  bim_tvabim_state(j->c, &j->di, &j->dop, &j->st);
  if (bim_tvabim_au_parse(j->c, &j->ns, &j->st, p, len, &f, &n, &j->err)) return decode_failed(j);
  if (!(parts = calloc(n ? n : 1, sizeof *parts))) {
    log_code("BE500101", "out of memory");
    goto out;
  }
  for (size_t i = 0; i < n; i++)
    if (f[i].root) {
      job_count_fragment(j, f[i].code);
      parts[k++] = (bim_tvapart_t){f[i].code, f[i].root};
      f[i].root = NULL;
    }
  if (bim_tvafrag_join(j->c, &j->ns, parts, k, &doc)) {
    bim_fail(&j->err, BIM_TVABIM_ERR, "bim: TVA BiM %s", "fragments not joinable");
    decode_failed(j);
    goto out;
  }
  rc = write_xml(j, doc, out);
  if (!rc && j->cfg->verbose) {
    log_code("BD700171", "%zu fragments read", n);
    job_log_fragments(j);
  }
out:
  bim_node_free(doc);
  bim_tvafrag_free(parts, k);
  bim_tvabim_au_free(f, n);
  return rc;
}

static int decode_generic(job_t *j, const unsigned char *p, size_t len, FILE *out) {
  bim_au_state_t as;
  bim_au_t au;
  size_t used;
  int rc = -1;
  bim_tvabim_state(j->c, &j->di, &j->dop, &j->st);
  j->st.opts.mpegb = job_mpegb(j->cfg);
  if (bim_fuu_state_aod_init(&j->st, &j->err)) return decode_failed(j);
  if (bim_au_state_init(&as, &j->di)) {
    log_code("BE500101", "out of memory");
    return -1;
  }
  if (bim_au_parse(&as, p, len, &au, &used, &j->err)) {
    decode_failed(j);
    bim_au_state_free(&as);
    return -1;
  }
  for (size_t i = 0; i < au.nfuus; i++) {
    bim_fuu_t u;
    if (bim_fuu_parse(&j->st, &au.fuus[i], &u, &j->err)) {
      decode_failed(j);
      goto out;
    }
    if (bim_fuu_apply(&j->st, &u, &j->err)) {
      bim_fuu_free(&u);
      decode_failed(j);
      goto out;
    }
    bim_fuu_free(&u);
  }
  if (!j->st.doc) {
    bim_fail(&j->err, "LE400306", "bim: FUU %s", "no document built");
    decode_failed(j);
    goto out;
  }
  rc = write_xml(j, j->st.doc, out);
  if (!rc && j->cfg->verbose) log_code("BD700171", "%zu fragments read", au.nfuus);
out:
  bim_au_free(&au);
  bim_au_state_free(&as);
  return rc;
}

int job_decode(job_t *j, FILE *in, FILE *out) {
  const config_t *cfg = j->cfg;
  const unsigned char *p;
  bim_tvainit_t ti = {0};
  size_t len, used = 0, di_used;
  if (read_all(in, &j->buf, &len)) {
    log_code("BE000123", "read failed");
    return -1;
  }
  p = (const unsigned char *)j->buf;
  if (cfg->profile == PROFILE_DVB_TVA) return decode_dvbtva(j, p, len, out);
  if (cfg->profile != PROFILE_GENERIC) {
    unsigned want = cfg->profile == PROFILE_TVA ? BIM_TVAINIT_TVA : BIM_TVAINIT_DVB;
    if (bim_tvainit_parse(p, len, &ti, &used, &j->err) || ti.encoding_version != want) {
      log_code("BE400242", "missing or unsupported DVB-TVA-init");
      return -1;
    }
  }
  if (bim_decoderinit_parse(p + used, len - used, cfg->profile == PROFILE_GENERIC && job_mpegb(cfg), NULL, NULL, &j->di, &di_used, &j->err)) return decode_failed(j);
  j->di_parsed = 1;
  if (!j->di.nschemas || !j->di.schemas[0].uri) {
    bim_fail(&j->err, BIM_DVBTVA_ERR, "bim: DVB-TVA %s", "DecoderInit without SchemaURI");
    return decode_failed(j);
  }
  j->root_ns = j->di.schemas[0].uri;
  if (cfg->profile == PROFILE_DVB_TVA || cfg->profile == PROFILE_TVA) j->ns.tva = j->root_ns;
  if (job_schema_load(j, j->root_ns) || job_schema_compile(j)) return -1;
  if (cfg->profile != PROFILE_SDNS) {
    j->nschemas = j->di.nschemas;
    for (size_t k = 0; k < j->nschemas && k < DVBTVA_SCHEMAS; k++) j->uris[k] = j->di.schemas[k].uri;
    if (job_schemas_open(j)) return -1;
  }
  p += used + di_used;
  len -= used + di_used;
  if (cfg->profile == PROFILE_TVA) {
    j->dop.zbuf_size = bim_tvainit_buffer_size(&ti);
    return decode_tva(j, p, len, out);
  }
  if (cfg->profile == PROFILE_GENERIC) return decode_generic(j, p, len, out);
  return decode_sdns(j, p, len, out);
}
