/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef DVBIPITOOLS_SDNS_ZLIB
#include "lib/helper/zutil.h"
#endif
#include "lib/helper/log.h"
#include "lib/helper/xml_util.h"
#include "lib/bim/nodexml.h"
#include "lib/bim/sdns.h"
#include "lib/sys/ioutil.h"

#ifdef DVBIPITOOLS_SDNS_ZLIB
#define SDNS_INFLATE zinflate
#else
#define SDNS_INFLATE NULL
#endif

#define SDNS_PREFIXES_MAX 16

const sdns_set_t *sdns_set_find(const char *ns) {
  for (int i = 0; i < sdns_nsets; i++)
    if (!strcmp(sdns_sets[i].ns, ns)) return &sdns_sets[i];
  return NULL;
}

int sdns_prefixes(const sdns_set_t *set, bim_nsdecl_t *out, int cap) {
  int n = set->nprefixes < cap ? set->nprefixes : cap;
  for (int i = 0; i < n; i++) out[i] = (bim_nsdecl_t){set->prefixes[i].prefix, set->prefixes[i].uri};
  return n;
}

/* exact: only zero padding left, no undelivered string bytes */
static bim_node_t *decode_with(const sdns_set_t *set, const unsigned char *p, size_t len) {
  bitreader_t br;
  bim_node_t *root = NULL;
  size_t zleft = 0;
  uint64_t pad;
  bim_schema_t sch = {.subs = set->subtypes, .nsubs = set->nsubtypes, .known = 1};
  bim_decode_opts_t opts = {.schemas = &sch, .nschemas = 1, .inflate = SDNS_INFLATE, .zleft = &zleft, .mpegb = 1, .lax_schema = -1};
  size_t left;
  bitreader_init(&br, p, len);
  if (bim_decode_payload(set->root, "ServiceDiscovery", &opts, &br, NULL, &root)) return NULL;
  root->ns = set->ns;
  left = bitreader_bits_left(&br);
  if (zleft || left >= 8 || bitreader_get(&br, (int)left, &pad) || pad) {
    bim_node_free(root);
    return NULL;
  }
  return root;
}

int sdns_bim_to_xml(const unsigned char *au, size_t len, const char *ns, char **xml, size_t *xml_len) {
  bitreader_t br;
  uint64_t nfuu;
  uint64_t flen;
  uint64_t ctx;
  const unsigned char *payload;
  const sdns_set_t *set = NULL;
  bim_node_t *root = NULL;
  bim_nsdecl_t pre[SDNS_PREFIXES_MAX];
  bim_xml_opts_t xo = {.prefixes = pre, .declaration = 1};
  int rc;
  bitreader_init(&br, au, len);
  /* TS 102 034 5.5.2.3 */
  if (bitreader_get_vluimsbf8(&br, &nfuu) || nfuu != 1 || bitreader_fuu_header(&br, &flen, &ctx) || ctx != SDNS_CTXPATH_SERVICE_DISCOVERY ||
      flen != bitreader_bits_left(&br) / 8)
    return -1;
  payload = au + br.byte_pos;
  if (ns) {
    set = sdns_set_find(ns);
    if (set) root = decode_with(set, payload, (size_t)flen);
  } else {
    for (int i = 0; i < sdns_nsets && !root; i++) {
      set = &sdns_sets[i];
      root = decode_with(set, payload, (size_t)flen);
    }
  }
  if (!root) return -1;
  xo.nprefixes = sdns_prefixes(set, pre, SDNS_PREFIXES_MAX);
  rc = bim_node_to_xml(root, &xo, xml, xml_len);
  bim_node_free(root);
  return rc ? -1 : 0;
}

int sdns_segment_xml(unsigned compr, const unsigned char *data, size_t len, const char *ns, const char **xml, size_t *xml_len, char **owned) {
  *owned = NULL;
  if (compr > 1) return -2;
  if (compr == 1) {
    if (sdns_bim_to_xml(data, len, ns, owned, xml_len)) return -1;
    *xml = *owned;
    return 0;
  }
  *xml = (const char *)data;
  *xml_len = len;
  return 0;
}

int sdns_doc_open(bim_sdnsdoc_t *d, const char *prefix, const char *const *paths, int npaths, const char *ns) {
  const char *culprit = NULL;
  const sdns_set_t *set;
  int rc = bim_sdnsdoc_open(d, paths, npaths, ns, &culprit);
  if (rc == -3) {
    char code[16];
    snprintf(code, sizeof code, "%s000761", prefix);
    log_code(code, "no namespace for the schema, give a schema file or a namespace");
  } else if (rc) {
    xsd_load_report(prefix, prefix, rc, culprit, ns, d->set);
  }
  if (rc) {
    bim_sdnsdoc_close(d);
    return -1;
  }
  d->inflate = SDNS_INFLATE;
  if ((set = sdns_set_find(d->ns))) d->nprefixes = sdns_prefixes(set, d->pfx, BIM_SDNSDOC_PREFIXES);
  d->prefixes = d->pfx;
  return 0;
}

int sdns_doc_segment_xml(const bim_sdnsdoc_t *d, unsigned compr, const unsigned char *data, size_t len, const char *ns, const char **xml, size_t *xml_len, char **owned) {
  return d ? bim_sdnsdoc_segment_xml(d, compr, data, len, xml, xml_len, owned) : sdns_segment_xml(compr, data, len, ns, xml, xml_len, owned);
}
