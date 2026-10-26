/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/beutil.h"
#include "lib/bim/bimau.h"
#include "lib/bim/bitwriter.h"
#include "lib/bim/dvbstream.h"
#include "lib/bim/dvbtva.h"
#include "lib/bim/sdnsbim.h"
#include "lib/bim/tvabim.h"
#include "lib/bim/tvainit.h"
#include "lib/helper/log.h"
#include "lib/sys/ioutil.h"
#include "job.h"
#include "version.h"

#define TOPMOST_PATH 2

/* encoder detail appended to err message */
static void add_diag(job_t *j) {
  size_t n = strlen(j->err.msg);
  if (j->diag[0] && n + 3 < sizeof j->err.msg) snprintf(j->err.msg + n, sizeof j->err.msg - n, ": %s", j->diag);
}

static int encode_failed(job_t *j) {
  add_diag(j);
  log_code("BE400315", "BiM encoding failed: %s", j->err.msg);
  return -1;
}

int job_put_all(FILE *out, const unsigned char *p, size_t n) {
  if (n && fwrite(p, 1, n, out) != n) {
    log_code("BE100103", "write failed");
    return -1;
  }
  return 0;
}

static int init_bytes(job_t *j, bitwriter_t *bw) {
  const config_t *cfg = j->cfg;
  bim_tvainit_t ti = {.encoding_version = cfg->profile == PROFILE_TVA ? BIM_TVAINIT_TVA : BIM_TVAINIT_DVB, .char_encoding = 1};
  if (cfg->profile == PROFILE_TVA || cfg->profile == PROFILE_SDNS) {
    ti.has_buffer_size = cfg->zbuf_size != 0;
    ti.buffer_size = cfg->zbuf_size;
  }
  if (cfg->profile != PROFILE_GENERIC && bim_tvainit_write(&ti, bw, &j->err)) return encode_failed(j);
  return bim_decoderinit_write(&j->di, NULL, NULL, bw, &j->err) ? encode_failed(j) : 0;
}

/* init, then access unit alone or length framed with string repository */
static int emit(job_t *j, FILE *out, const bitwriter_t *au, strrepo_writer_t *sw, size_t nfuu) {
  bitwriter_t hd;
  const unsigned char *h;
  const unsigned char *a;
  const unsigned char *s = NULL;
  size_t hl, al, sl = 0;
  unsigned char len[4];
  int framed = j->cfg->profile == PROFILE_DVB_TVA || j->cfg->profile == PROFILE_SDNS;
  int rc;
  bitwriter_init(&hd);
  if (init_bytes(j, &hd)) {
    bitwriter_free(&hd);
    return -1;
  }
  h = bitwriter_data(&hd, &hl);
  a = bitwriter_data((bitwriter_t *)au, &al);
  if (framed && sw) s = strrepo_writer_data(sw, &sl);
  if (al > UINT32_MAX || sl > UINT32_MAX) {
    log_code("BE400108", "encoded size exceeds 4 GiB format limit: %zu+%zu bytes", al, sl);
    bitwriter_free(&hd);
    return -1;
  }
  be32_put(len, (uint32_t)al);
  rc = job_put_all(out, h, hl) || (framed && job_put_all(out, len, 4)) || job_put_all(out, a, al) || (framed && job_put_all(out, s, sl));
  bitwriter_free(&hd);
  if (!rc && j->cfg->verbose) {
    log_code("BD700170", "%zu fragments -> %zu+%zu bytes", nfuu, al, sl);
    job_log_fragments(j);
  }
  return rc ? -1 : 0;
}

static int encode_dvbtva(job_t *j, const bim_node_t *root, FILE *out) {
  dvbtva_roots_t r = {j->c, &j->ns};
  unsigned char *buf = NULL;
  size_t len = 0;
  size_t sizes[2] = {0, 0};
  size_t n = 0;
  int rc = dvbstream_write(&r, &j->eo, root, 1, &buf, &len, j->nfrag, sizes, &j->err);
  if (rc == DVBSTREAM_ROOT) {
    log_code("BE400314", "root element %s is not coded by profile %s", root->name, "dvb-tva");
    return -1;
  }
  if (rc) {
    add_diag(j);
    dvbstream_report("BE", 1, rc, NULL, 0, sizes, &j->err);
    return -1;
  }
  rc = job_put_all(out, buf, len);
  free(buf);
  if (rc || !j->cfg->verbose) return rc ? -1 : 0;
  for (size_t i = 0; i < FRAG_CODES; i++) n += j->nfrag[i];
  log_code("BD700170", "%zu fragments -> %zu+%zu bytes", n, sizes[0], sizes[1]);
  job_log_fragments(j);
  return 0;
}

static int encode_sdns(job_t *j, const bim_node_t *root, FILE *out) {
  bim_sdnsroots_t sr = {j->c, j->root_ns};
  dvbtva_fuu_t one = {BIM_SDNS_CONTEXT_PATH, (bim_node_t *)root};
  bitwriter_t bw;
  strrepo_writer_t sw;
  int rc = -1;
  bitwriter_init(&bw);
  if (strrepo_writer_init(&sw)) {
    log_code("BE500101", "out of memory");
    bitwriter_free(&bw);
    return -1;
  }
  if (dvbtva_au_write(bim_sdnsbim_root, &sr, &j->eo, &one, 1, &bw, &sw, NULL, &j->err)) encode_failed(j);
  else rc = emit(j, out, &bw, &sw, 1);
  strrepo_writer_free(&sw);
  bitwriter_free(&bw);
  return rc;
}

static int encode_tva(job_t *j, const bim_node_t *root, FILE *out) {
  bim_tvapart_t *parts = NULL;
  bim_tvabim_fuu_t *f = NULL;
  bitwriter_t bw;
  size_t n = 0;
  int sp = bim_tvafrag_split(j->c, &j->ns, root, &parts, &n);
  int rc = -1;
  bitwriter_init(&bw);
  if (sp == -2) {
    log_code("BE400314", "root element %s is not coded by profile %s", root->name, "tva");
    goto out;
  }
  if (sp || !(f = calloc(n ? n : 1, sizeof *f))) {
    log_code("BE500101", "out of memory");
    goto out;
  }
  for (size_t i = 0; i < n; i++) {
    f[i] = (bim_tvabim_fuu_t){parts[i].code, BIM_CMD_REPLACE, 0, parts[i].root};
    job_count_fragment(j, parts[i].code);
  }
  bim_tvabim_state(j->c, &j->di, &j->dop, &j->st);
  if (bim_tvabim_au_write(j->c, &j->ns, &j->st, &j->eo, f, n, &bw, NULL, &j->err)) {
    encode_failed(j);
    goto out;
  }
  rc = emit(j, out, &bw, NULL, n);
out:
  free(f);
  bim_tvafrag_free(parts, parts ? n : 0);
  bitwriter_free(&bw);
  return rc;
}

static int encode_generic(job_t *j, const bim_node_t *root, FILE *out) {
  const bim_elem_t *e = xsd_compiled_elem(j->c, root->ns, root->name);
  bim_tbc_t tbcs[TOPMOST_PATH] = {{.kind = BIM_TBC_END}, {.kind = BIM_TBC_ELEM}};
  uint64_t pos[TOPMOST_PATH] = {0};
  bim_node_t *payload = (bim_node_t *)root;
  bim_fuu_t u = {.command = BIM_CMD_ADD, .mode = 1, .tbcs = tbcs, .ntbcs = TOPMOST_PATH, .npayloads = 1, .pos = pos, .payloads = &payload};
  bim_au_state_t as;
  bim_au_t au = {0};
  bim_encode_opts_t eo = j->eo;
  bitwriter_t fb;
  bitwriter_t bw;
  strrepo_writer_t sw;
  bim_bits_t bits;
  size_t len;
  int rc = -1;
  if (!e) {
    log_code("BE400314", "root element %s is not coded by profile %s", root->name, "generic");
    return -1;
  }
  tbcs[1].name = e->name;
  tbcs[1].ns = e->ns;
  tbcs[1].type = e->type;
  eo.mpegb = job_mpegb(j->cfg);
  eo.cast |= bim_tree_has_cast(root);
  bitwriter_init(&fb);
  bitwriter_init(&bw);
  bim_tvabim_state(j->c, &j->di, &j->dop, &j->st);
  j->st.opts.mpegb = job_mpegb(j->cfg);
  if (strrepo_writer_init(&sw)) {
    log_code("BE500101", "out of memory");
    goto out;
  }
  if (bim_au_state_init(&as, &j->di)) {
    log_code("BE500101", "out of memory");
    strrepo_writer_free(&sw);
    goto out;
  }
  if (bim_fuu_write(&j->st, &u, &eo, &sw, &fb, &j->err)) {
    encode_failed(j);
  } else if (sw.len > 1) {
    bim_fail(&j->err, BIM_TVABIM_ERR, "bim: TVA BiM %s", "needs an external string buffer");
    encode_failed(j);
  } else {
    bits.bits = (unsigned char *)bitwriter_data(&fb, &len);
    bits.nbits = (uint64_t)len * 8;
    au.fuus = &bits;
    au.nfuus = 1;
    if (bim_au_write(&as, &au, &bw, &j->err)) encode_failed(j);
    else rc = emit(j, out, &bw, NULL, 1);
  }
  bim_au_state_free(&as);
  strrepo_writer_free(&sw);
out:
  bitwriter_free(&fb);
  bitwriter_free(&bw);
  return rc;
}

int job_encode(job_t *j, FILE *in, FILE *out) {
  const bim_node_t *root;
  size_t len;
  if (read_all(in, &j->buf, &len)) {
    log_code("BE000123", "read failed");
    return -1;
  }
  switch (xml_to_utf8(&j->buf, &len, NULL, 0)) {
    case 0: break;
    case -2:
      log_code("BE500101", "out of memory");
      return -1;
    default:
      log_code("BE400249", "bim: %s: unsupported encoding", j->cfg->input_path);
      return -1;
  }
  if (xml_doc_parse(j->buf, len, &j->xml, NULL) || !(root = xml_doc_root(j->xml))) {
    log_code("BE000756", "input is not well-formed XML");
    return -1;
  }
  if (!root->ns) {
    log_code("BE400314", "root element %s is not coded by profile %s", root->name, "any: no namespace");
    return -1;
  }
  j->root_ns = root->ns;
  if (j->cfg->profile == PROFILE_DVB_TVA || j->cfg->profile == PROFILE_TVA) j->ns.tva = root->ns;
  if (job_schema_load(j, root->ns) || job_schema_compile(j) || job_schemas_encode(j, root)) return -1;
  if (job_cast_bind(j, (bim_node_t *)root)) return -1;
  if (j->cfg->profile == PROFILE_SDNS) {
    if (bim_sdnsbim_decoderinit(j->c, root->ns, &j->di)) {
      log_code("BE500101", "out of memory");
      return -1;
    }
  } else if (bim_tvainit_decoderinit(root->ns, j->eo.schema_uris, j->eo.nschema_uris, &j->di)) {
    log_code("BE500101", "out of memory");
    return -1;
  }
  if (j->cfg->profile == PROFILE_GENERIC) {
    j->di.mpegb = job_mpegb(j->cfg);
    j->di.reserved = j->di.mpegb ? 0xF : 0x1F;
  }
  if (j->cfg->profile == PROFILE_TVA) return encode_tva(j, root, out);
  if (j->cfg->profile == PROFILE_GENERIC) return encode_generic(j, root, out);
  return j->cfg->profile == PROFILE_SDNS ? encode_sdns(j, root, out) : encode_dvbtva(j, root, out);
}
