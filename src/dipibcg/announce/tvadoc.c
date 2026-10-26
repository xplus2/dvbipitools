/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/dvbstream.h"
#include "lib/helper/fileutil.h"
#include "lib/helper/xml_util.h"
#include "dipibcg/announce/announce_int.h"

#define CODE_TVA_MAIN 0x000E

static int read_input(const config_t *cfg, char **buf, size_t *len) {
  FILE *f = fileutil_open_std(cfg->input_path, "r");
  int rc;
  if (!f) {
    log_code("GE000100", "cannot open %s", cfg->input_path);
    return -1;
  }
  rc = read_all(f, buf, len);
  if (f != stdin) fclose(f);
  if (rc) log_code("GE000123", "read failed");
  return rc;
}

/* TVAMain tree of input, ns out, s opened for -f bim. -1 error (logged) */
static int read_tree(const config_t *cfg, bcg_schema_t *s, const char *buf, size_t len, char *ns, size_t cap, int startup, xml_doc_t **x, bim_node_t **tree) {
  const bim_node_t *root;
  size_t used = 0;
  dvbstream_head_t head;
  bim_err_t err;
  const char *uris[DVBTVA_SCHEMAS];
  int rc;
  *x = NULL;
  *tree = NULL;
  if (cfg->format == FMT_TVA) {
    if (xml_doc_parse(buf, len, x, NULL) || !(root = xml_doc_root(*x)) || !root->ns || strlen(root->ns) >= cap) {
      log_code("GE000756", "input is not well-formed XML");
      return -1;
    }
    memcpy(ns, root->ns, strlen(root->ns) + 1);
    return 0;
  }
  if ((rc = dvbstream_head((const unsigned char *)buf, len, &head, &err))) {
    dvbstream_report("GE", 0, rc, (const unsigned char *)buf, len, NULL, &err);
    return -1;
  }
  used = head.used;
  if (strlen(head.uri[0]) >= cap) {
    log_code("GE400310", "bim: DVB-TVA %s", "DecoderInit without usable SchemaURI");
    return -1;
  }
  memcpy(ns, head.uri[0], strlen(head.uri[0]) + 1);
  for (size_t k = 0; k < head.nschemas; k++) uris[k] = head.uri[k];
  if (bcg_schema_open(s, cfg, ns, startup)) return -1;
  if (bcg_schema_extras(s, cfg, uris, head.nschemas, startup) || bcg_schema_wire(s, uris, head.nschemas)) {
    bcg_schema_close(s);
    return -1;
  }
  if (strcmp(ns, s->ns)) return 0;
  {
    dvbtva_roots_t r = {s->au.c, &s->au.ns};
    rc = dvbstream_read(&r, &s->au.dop, (const unsigned char *)buf + used, len - used, tree, NULL, &err);
    if (rc) dvbstream_report("GE", 0, rc, (const unsigned char *)buf + used, len - used, NULL, &err);
  }
  return rc ? -1 : 0;
}

int tvadoc_load(const config_t *cfg, int startup, const char *want_ns, bcg_schema_t **sch, dvbtva_fuu_t **out, int *nout) {
  bcg_schema_t *s = calloc(1, sizeof *s);
  char *buf = NULL;
  size_t len = 0;
  char ns[sizeof s->ns];
  xml_doc_t *x = NULL;
  bim_node_t *tree = NULL;
  dvbtva_fuu_t *parts = NULL;
  size_t n = 0;
  size_t k = 0;
  int rc = -1;
  int sp;
  if (!s) {
    log_code("GE500101", "out of memory");
    return -1;
  }
  if (read_input(cfg, &buf, &len) || read_tree(cfg, s, buf, len, ns, sizeof ns, startup, &x, &tree)) goto out;
  if (!s->set && bcg_schema_open(s, cfg, ns, startup)) goto out;
  if (strcmp(ns, s->ns) || (want_ns && strcmp(ns, want_ns))) {
    log_code("GE000767", "input namespace %s differs from the schema namespace %s", ns, want_ns && strcmp(ns, want_ns) ? want_ns : s->ns);
    goto out;
  }
  if (!tree && (bcg_schema_encode(s, cfg, xml_doc_root(x), startup) || bcg_cast_bind(s, (bim_node_t *)xml_doc_root(x), startup))) goto out;
  sp = bim_tvafrag_split(s->au.c, &s->au.ns, tree ? tree : xml_doc_root(x), &parts, &n);
  if (sp) {
    if (sp == -2) log_code("GE400314", "root element %s is not coded by profile %s", (tree ? tree : xml_doc_root(x))->name, "dvb-tva");
    else log_code("GE500101", "out of memory");
    goto out;
  }
  /* TS 102 323 8 content rules */
  for (size_t i = 0; i <= n; i++) {
    bim_err_t err;
    if (i < n ? dvbtva_check_fragment(&s->au.ns, &parts[i], &err) : dvbtva_check_set(&s->au.ns, parts, n, &err)) {
      log_code(startup ? "GS400318" : "GE400318", "DVB-TVA content rule violated: %s", err.msg);
      goto out;
    }
  }
  for (size_t i = 0; i < n; i++) {
    if (parts[i].code == CODE_TVA_MAIN) bim_node_free(parts[i].root);
    else parts[k++] = parts[i];
  }
  *sch = s;
  *out = parts;
  *nout = (int)k;
  parts = NULL;
  s = NULL;
  rc = 0;
out:
  if (parts) dvbtva_au_free(parts, n);
  if (s) {
    if (s->set) bcg_schema_close(s);
    free(s);
  }
  bim_node_free(tree);
  xml_doc_free(x);
  free(buf);
  return rc;
}
