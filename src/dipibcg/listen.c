/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/tva/mapping.h"
#include "lib/bim/bcgau.h"
#include "lib/bim/bcgip.h"
#include "lib/bim/bimreader.h"
#include "lib/bim/container.h"
#include "lib/bim/dvbstream.h"
#include "lib/bim/nodexml.h"
#include "lib/bim/index.h"
#include "lib/bim/strrepo.h"
#include "lib/helper/fileutil.h"
#include "lib/helper/log.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/tva/bcg_doc.h"
#include "lib/tva/xmltv.h"
#include "listen.h"
#include "schema.h"
#include "version.h"


void write_csvmap(const char *path, const bcg_doc_t *doc) {
  FILE *f = fopen(path, "w");
  if (!f) {
    log_code("GS100101", "cannot open %s for writing", path);
    return;
  }
  for (int i = 0; i < doc->channel_count; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    if (!c->uri[0]) continue;
    if (strpbrk(c->id, "\r\n") || strpbrk(c->uri, "\r\n")) {
      log_code("GW100146", "channel %s: line break in id or uri, skipped in csv map", c->id);
      continue;
    }
    mapping_write_row(f, c);
  }
  if (fclose(f)) log_code("GE100102", "error writing %s", path);
}

typedef struct {
  dvbstp_key_t key;
  unsigned version; /* last segment_version seen */
  int ok;           /* parsed holds a decode */
  bcgau_parsed_t parsed;
} seg_slot_t;

static seg_slot_t *slot_find(seg_slot_t *slots, int n, const dvbstp_header_t *h) {
  for (int i = 0; i < n; i++)
    if (dvbstp_key_match(&slots[i].key, h)) return &slots[i];
  return NULL;
}

/* per (provider, segment_id): latest decode replaces previous. p NULL: version undecodable, previous decode kept.
   Takes *p on success. 0 ok, -1 oom */
static int slot_store(seg_slot_t **slots, int *n, const dvbstp_header_t *h, const bcgau_parsed_t *p) {
  seg_slot_t *s = slot_find(*slots, *n, h);
  if (!s) {
    seg_slot_t *np = realloc(*slots, sizeof **slots * (size_t)(*n + 1));
    if (!np) return -1;
    *slots = np;
    s = &np[(*n)++];
    memset(s, 0, sizeof *s);
    dvbstp_key_set(&s->key, h);
  }
  s->version = h->segment_version;
  if (!p) return 0;
  bcgau_parsed_free(&s->parsed);
  s->parsed = *p;
  s->ok = 1;
  return 0;
}

/* latest container per segment_id, index + data */
typedef struct {
  unsigned id;
  unsigned version;
  unsigned char *buf;
  size_t len;
  int is_data;
} cont_slot_t;

typedef struct {
  cont_slot_t *c;
  int n;
} cont_set_t;

/* takes buf. 0 ok, -1 oom (buf freed) */
static int cont_store(cont_set_t *s, unsigned id, unsigned version, unsigned char *buf, size_t len, int is_data) {
  cont_slot_t *np;
  for (int i = 0; i < s->n; i++) {
    if (s->c[i].id != id || s->c[i].is_data != is_data) continue;
    free(s->c[i].buf);
    s->c[i].version = version;
    s->c[i].buf = buf;
    s->c[i].len = len;
    return 0;
  }
  np = realloc(s->c, sizeof *np * (size_t)(s->n + 1));
  if (!np) {
    free(buf);
    return -1;
  }
  s->c = np;
  np[s->n++] = (cont_slot_t){id, version, buf, len, is_data};
  return 0;
}

static const cont_slot_t *cont_get(const cont_set_t *s, unsigned id, int is_data) {
  for (int i = 0; i < s->n; i++)
    if (s->c[i].id == id && s->c[i].is_data == is_data) return &s->c[i];
  return NULL;
}

static int ix_find(const void *ctx, unsigned container, unsigned type, unsigned id, const unsigned char **body, size_t *len) {
  const cont_slot_t *c = cont_get(ctx, container, 0);
  if (!c) c = cont_get(ctx, container, 1);
  return c && !bim_container_find(c->buf, c->len, type, id, body, len) ? 0 : -1;
}

static int ix_has(const void *ctx, unsigned container, uint32_t fragment) {
  const cont_slot_t *c = cont_get(ctx, container, 1);
  bim_fragref_t *refs;
  unsigned fmt;
  int n;
  int hit = 0;
  if (!c || bim_container_parse_refs(c->buf, c->len, &fmt, &refs, &n)) return 0;
  for (int i = 0; i < n && !hit; i++) hit = refs[i].frag_id == fragment;
  free(refs);
  return hit;
}

/* TS 102 323 9.5.1, receivers locate fragments via index */
static void index_report(const cont_set_t *s, int verbose) {
  bim_index_report_t r;
  int have_index = 0;
  for (int i = 0; i < s->n; i++) have_index |= !s->c[i].is_data;
  if (!have_index) return;
  if (bim_index_check(ix_find, ix_has, s, &r)) {
    log_code("GW400246", "index containers without index list (container 0x0000)");
    return;
  }
  if (r.missing || r.malformed || r.misordered || r.unresolved)
    log_code("GW400247", "index check: %d indices, %d entries, %d structures missing, %d malformed, %d misordered, %d unresolved locators", r.indexes, r.entries, r.missing, r.malformed, r.misordered, r.unresolved);
  else if (verbose)
    log_code("GD700169", "index check: %d indices ok, %d entries, %d other indices ignored", r.indexes, r.entries, r.ignored);
}

typedef struct {
  const config_t *cfg;
  unsigned segments;
  unsigned captures;
  seg_slot_t *slots;
  int nslots;
  bcg_schema_t sch;
  int have_schema;
  int bim_init; /* schema from DVB-TVA-init, encode wired to its schemas */
  int schema_tried;
  int bad_init_logged;
  int bad_main_logged;
  bcgau_parsed_t main; /* TVAMain fragment */
  int main_seen;        /* main_version processed */
  unsigned main_version;
  cont_set_t conts;
} listen_ctx_t;

static int decode_container(const listen_ctx_t *c, const unsigned char *data, size_t len, unsigned char **unwrapped, size_t *unwrapped_len, bcgau_parsed_t *out) {
  const unsigned char *au;
  const unsigned char *sr_bytes;
  size_t au_len;
  size_t sr_len;
  bim_fragref_t *refs = NULL;
  unsigned fmt = 0;
  int nrefs = 0;
  bitreader_t br;
  strrepo_reader_t sr;
  int rc = -1;
  if (bim_bcg_unwrap(data, len, BCG_INFLATE, unwrapped, unwrapped_len)) return -1;
  if (!bim_container_parse(*unwrapped, *unwrapped_len, &au, &au_len, &sr_bytes, &sr_len) && sr_bytes && !bim_container_parse_refs(*unwrapped, *unwrapped_len, &fmt, &refs, &nrefs)) {
    bitreader_init(&br, au, au_len);
    bim_fragref_t *use_refs = NULL;
    if (fmt == BIM_FRAGREF_DVB_BIM) use_refs = refs;
    rc = strrepo_reader_init(&sr, sr_bytes, sr_len) || bcgau_parse(&c->sch.au, &br, &sr, use_refs, nrefs, out) ? -1 : 0;
  }
  free(refs);
  if (rc) {
    free(*unwrapped);
    *unwrapped = NULL;
  }
  return rc;
}

/* schema from namespace of first plain document */
static int plain_schema(listen_ctx_t *c, const unsigned char *data, size_t len) {
  xml_doc_t *x = NULL;
  const bim_node_t *root;
  int rc = -1;
  if (c->have_schema) return 0;
  if (c->schema_tried) goto out;
  if (xml_doc_parse((const char *)data, len, &x, NULL)) goto out;
  if (!(root = xml_doc_root(x))) goto out;
  if (!root->ns) goto out;
  c->schema_tried = 1;
  rc = bcg_schema_open(&c->sch, c->cfg, root->ns, 0) ? -1 : (c->have_schema = 1, 0);
out:
  xml_doc_free(x);
  return rc;
}

/* segment fragments: container (compr 1) or TVA XML (compr 0). *unwrapped: container B for index check, NULL plain */
static int parse_segment(listen_ctx_t *c, const dvbstp_header_t *hdr, const unsigned char *data, size_t len, unsigned char **unwrapped, size_t *unwrapped_len, bcgau_parsed_t *out) {
  *unwrapped = NULL;
  if (hdr->compr == 1) return decode_container(c, data, len, unwrapped, unwrapped_len, out);
  return plain_schema(c, data, len) || bcgau_parse_xml(&c->sch.au, (const char *)data, len, out) ? -1 : 0;
}

static int has_publisher(const listen_ctx_t *c, const bcgau_parsed_t *p) {
  bcg_doc_t d;
  int ok;
  bcg_doc_init(&d);
  ok = !bcgau_apply(&c->sch.au, p, &d) && d.publisher[0];
  bcg_doc_free(&d);
  return ok;
}

static int on_main(listen_ctx_t *c, const dvbstp_header_t *hdr, const unsigned char *data, size_t len) {
  unsigned char *unwrapped;
  size_t unwrapped_len;
  bcgau_parsed_t p;
  int ok;
  if (c->main_seen && c->main_version == hdr->segment_version) return 0;
  c->main_seen = 1;
  c->main_version = hdr->segment_version;
  ok = !parse_segment(c, hdr, data, len, &unwrapped, &unwrapped_len, &p);
  free(unwrapped);
  if (ok) ok = has_publisher(c, &p);
  if (ok) {
    bcgau_parsed_free(&c->main);
    c->main = p;
  }
  /* TS 102 323 9.4.2.2: default TVAMain then */
  if (!ok) {
    if (!c->bad_main_logged) log_code("GW400245", "TVAMain fragment not decodable (%zu bytes), default used", len);
    c->bad_main_logged++;
  }
  return 0;
}

static int on_segment(void *ctx, const dvbstp_header_t *hdr, const unsigned char *data, size_t len) {
  listen_ctx_t *c = ctx;
  unsigned char *unwrapped;
  size_t unwrapped_len;
  bcgau_parsed_t p;
  if (hdr->payload_id == DVBSTP_PAYLOAD_BCG_TVA_INIT && hdr->compr == 1) {
    bim_tvainit_t ti;
    bim_decoderinit_t di;
    bim_err_t err;
    int ok = 0;
    if (!bim_bcg_tvainit_parse(data, len, &ti, &di, &err)) {
      const char *uri = di.nschemas ? di.schemas[0].uri : NULL;
      if (uri && c->have_schema) {
        ok = !strcmp(c->sch.ns, uri);
      } else if (uri && !c->schema_tried) {
        c->schema_tried = 1;
        if (!bcg_schema_open(&c->sch, c->cfg, uri, 0)) {
          const char *uris[DVBTVA_SCHEMAS] = {0};
          for (size_t k = 0; k < di.nschemas && k < DVBTVA_SCHEMAS; k++) uris[k] = di.schemas[k].uri ? di.schemas[k].uri : "";
          if (strcmp(c->sch.ns, uri) || bcg_schema_extras(&c->sch, c->cfg, uris, di.nschemas, 0) || bcg_schema_wire(&c->sch, uris, di.nschemas)) bcg_schema_close(&c->sch);
          else {
            c->have_schema = 1;
            c->bim_init = 1;
            ok = 1;
          }
        }
      }
      bim_decoderinit_free(&di);
    }
    if (!ok) {
      if (!c->bad_init_logged) log_code("GW400240", "unsupported DVB-TVA-init (%zu bytes), containers ignored", len);
      c->bad_init_logged++;
    }
    return 0;
  }
  /* TS 102 539 7.3, table 3: init first for BiM, plain XML needs none */
  if (hdr->compr > 1 || (hdr->compr == 1 && !c->have_schema)) return 0;
  if (hdr->payload_id == DVBSTP_PAYLOAD_BCG_TVAMAIN) return on_main(c, hdr, data, len);
  if (hdr->payload_id == DVBSTP_PAYLOAD_BCG_INDEX_CONTAINER) {
    const cont_slot_t *k = cont_get(&c->conts, hdr->segment_id, 0);
    if (hdr->compr == 1 && !(k && k->version == hdr->segment_version) && !bim_bcg_unwrap(data, len, BCG_INFLATE, &unwrapped, &unwrapped_len) &&
        cont_store(&c->conts, hdr->segment_id, hdr->segment_version, unwrapped, unwrapped_len, 0))
      log_code("GS500101", "out of memory");
    return 0;
  }
  if (hdr->payload_id != DVBSTP_PAYLOAD_BCG_DATA_CONTAINER && hdr->payload_id != DVBSTP_PAYLOAD_BCG_DATA_AND_INDEX) return 0;
  {
    const seg_slot_t *s = slot_find(c->slots, c->nslots, hdr);
    if (s && s->version == hdr->segment_version) return 0;
  }
  c->segments++;
  if (parse_segment(c, hdr, data, len, &unwrapped, &unwrapped_len, &p)) {
    if (slot_store(&c->slots, &c->nslots, hdr, NULL)) log_code("GS500101", "out of memory");
    return 0;
  }
  c->captures++;
  if (unwrapped && cont_store(&c->conts, hdr->segment_id, hdr->segment_version, unwrapped, unwrapped_len, 1)) log_code("GS500101", "out of memory");
  if (c->cfg->verbose) {
    bcg_doc_t d;
    bcg_doc_init(&d);
    bcgau_apply(&c->sch.au, &p, &d);
    log_code("GD700100", "segment %u: %d channels, %d programmes, %d fragments", c->segments, d.channel_count, d.programme_count, (int)p.n);
    bcg_doc_free(&d);
  }
  if (slot_store(&c->slots, &c->nslots, hdr, &p)) {
    bcgau_parsed_free(&p);
    log_code("GS500101", "out of memory");
  }
  return 0;
}

/* all slot fragments + TVAMain as 1 document, consumes trees */
static int merged_doc(listen_ctx_t *c, bim_node_t **doc) {
  size_t n = c->main.n;
  size_t k = 0;
  dvbtva_fuu_t *all;
  int rc;
  for (int i = 0; i < c->nslots; i++) n += c->slots[i].parsed.n;
  if (!(all = calloc(n + 1, sizeof *all))) return -1;
  for (size_t i = 0; i < c->main.n; i++) {
    all[k++] = c->main.f[i];
    c->main.f[i].root = NULL;
  }
  for (int s = 0; s < c->nslots; s++)
    for (size_t i = 0; i < c->slots[s].parsed.n; i++) {
      all[k++] = c->slots[s].parsed.f[i];
      c->slots[s].parsed.f[i].root = NULL;
    }
  rc = bim_tvafrag_join(c->sch.au.c, &c->sch.au.ns, all, k, doc);
  bim_tvafrag_free(all, k);
  return rc ? -1 : 0;
}

static void parsed_append(bcgau_parsed_t *all, const bcgau_parsed_t *p) {
  if (!p->n) return;
  memcpy(all->f + all->n, p->f, sizeof *p->f * p->n);
  if (p->tails) memcpy(all->tails + all->n, p->tails, sizeof *p->tails * p->n);
  all->n += p->n;
}

/* slots and TVAMain applied as one set: schedule texts from programmes of any container. 0 ok, -1 error */
static int apply_all(const listen_ctx_t *c, bcg_doc_t *doc) {
  bcgau_parsed_t all;
  size_t n = c->main.n;
  int rc;
  for (int i = 0; i < c->nslots; i++) n += c->slots[i].parsed.n;
  all.n = 0;
  all.f = calloc(n + 1, sizeof *all.f);
  all.tails = calloc(n + 1, sizeof *all.tails);
  if (!all.f || !all.tails) {
    free(all.f);
    free(all.tails);
    return -1;
  }
  for (int i = 0; i < c->nslots; i++) parsed_append(&all, &c->slots[i].parsed);
  parsed_append(&all, &c->main);
  rc = bcgau_apply(&c->sch.au, &all, doc);
  free(all.f);
  free(all.tails);
  return rc;
}

/* received guide in cfg->format to f, csv map */
static int write_doc(listen_ctx_t *c, FILE *f) {
  const config_t *cfg = c->cfg;
  bcg_doc_t doc;
  int rc = 0;
  if (cfg->format == FMT_XMLTV || cfg->csvmap_path) {
    bcg_doc_init(&doc);
    rc = apply_all(c, &doc);
    if (rc) log_code("GS500101", "out of memory");
    if (!rc && cfg->format == FMT_XMLTV) xmltv_write(f, &doc, TOOL_NAME);
    if (!rc && cfg->csvmap_path) write_csvmap(cfg->csvmap_path, &doc);
    bcg_doc_free(&doc);
  }
  if (rc || !f || cfg->format == FMT_XMLTV) return rc;
  {
    bim_node_t *tree;
    dvbtva_roots_t r = {c->sch.au.c, &c->sch.au.ns};
    bim_xml_opts_t xo = {.indent = 2, .declaration = 1};
    char *xml = NULL;
    unsigned char *bim = NULL;
    size_t len = 0;
    size_t sizes[2];
    bim_err_t err;
    if (merged_doc(c, &tree)) return -1;
    if (cfg->format == FMT_TVA) {
      rc = bim_node_to_xml(tree, &xo, &xml, &len);
      if (!rc && fwrite(xml, 1, len, f) != len) rc = -1;
    } else if (!c->bim_init && (bcg_schema_encode(&c->sch, cfg, tree, 0) || bcg_cast_bind(&c->sch, tree, 0))) {
      rc = -1;
    } else if ((rc = dvbstream_write(&r, &c->sch.au.eo, tree, 0, &bim, &len, NULL, sizes, &err))) {
      dvbstream_report("GE", 1, rc, NULL, 0, sizes, &err);
    } else if (fwrite(bim, 1, len, f) != len) {
      rc = -1;
    }
    free(xml);
    free(bim);
    bim_node_free(tree);
  }
  return rc;
}

int listen_run(const config_t *cfg) {
  char mcast[80];
  mcast_t *m;
  listen_ctx_t *c;
  int have_doc;
  int rc;
  int write_failed = 0;
  mcast_describe(cfg, mcast, sizeof mcast);
  m = mcast_open(cfg->family, cfg->mcast_group, cfg->mcast_port, cfg->iface, 500);
  if (!m) {
    log_code("GS500100", "cannot join %s", mcast);
    return 1;
  }
  c = calloc(1, sizeof *c);
  if (!c) {
    log_code("GS500101", "out of memory");
    mcast_close(m);
    return 1;
  }
  c->cfg = cfg;
  if (cfg->timeout_s) log_line("listening on %s for %lds", mcast, cfg->timeout_s);
  else log_line("listening on %s until stopped", mcast);
  rc = dvbstp_listen(m, cfg->timeout_s ? (double)cfg->timeout_s : INFINITY, on_segment, c);
  if (rc == -1) log_code("GE500240", "receive on %s failed, giving up", mcast);
  if (rc == -2) log_code("GS500101", "out of memory");
  have_doc = 0;
  for (int i = 0; i < c->nslots; i++) have_doc |= c->slots[i].ok;
  index_report(&c->conts, cfg->verbose);
  if (have_doc) {
    FILE *f = fileutil_open_std(cfg->output_path, "w");
    if (!f) log_code("GE100101", "cannot open %s for writing", cfg->output_path);
    write_failed = !f || write_doc(c, f);
    if (f && f != stdout && fclose(f)) {
      log_code("GE100102", "error writing %s", cfg->output_path);
      write_failed = 1;
    }
  }
  for (int i = 0; i < c->conts.n; i++) free(c->conts.c[i].buf);
  free(c->conts.c);
  for (int i = 0; i < c->nslots; i++) bcgau_parsed_free(&c->slots[i].parsed);
  free(c->slots);
  bcgau_parsed_free(&c->main);
  if (c->have_schema) bcg_schema_close(&c->sch);
  mcast_close(m);
  log_line("captured %u time%s in %u segment%s", c->captures, c->captures == 1 ? "" : "s", c->segments, c->segments == 1 ? "" : "s");
  free(c);
  return have_doc && !write_failed ? 0 : 1;
}
