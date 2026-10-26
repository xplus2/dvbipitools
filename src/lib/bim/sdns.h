/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_SDNS_H
#define DVBIPITOOLS_LIB_BIM_SDNS_H

#include <stddef.h>
#include "lib/bim/codec.h"
#include "lib/bim/sdnsbim.h"

/* xmlns prefix of element names */
typedef struct {
  const char *prefix;
  const char *uri;
} sdns_prefix_t;

/* BiM SD&S namespace, TS 102 034 5.5.2. root: type of context path 0x0030 */
typedef struct {
  const char *ns;
  const bim_type_t *root;
  const bim_subtypes_t *subtypes;
  int nsubtypes;
  const sdns_prefix_t *prefixes;
  int nprefixes;
} sdns_set_t;

#define SDNS_CTXPATH_SERVICE_DISCOVERY 0x0030

extern const sdns_set_t sdns_sets[]; /* newest first */
extern const int sdns_nsets;

/* NULL if ns unsupported */
const sdns_set_t *sdns_set_find(const char *ns);
/* prefixes of set as bim_nsdecl_t, at most cap. returns count */
int sdns_prefixes(const sdns_set_t *set, bim_nsdecl_t *out, int cap);

/* compr 001 seg to XML, *xml mallocd. ns NULL: try newest first, exact payload use wins. -1 not decodable, also w/o zlib */
int sdns_bim_to_xml(const unsigned char *au, size_t len, const char *ns, char **xml, size_t *xml_len);
/* bim_sdnsdoc_open + inflate, prefixes. prefix: log code type ("YS"). 0 ok, -1 error */
int sdns_doc_open(bim_sdnsdoc_t *d, const char *prefix, const char *const *paths, int npaths, const char *ns);
/* d NULL: sdns_segment_xml with compiled-in schemas, else bim_sdnsdoc_segment_xml */
int sdns_doc_segment_xml(const bim_sdnsdoc_t *d, unsigned compr, const unsigned char *data, size_t len, const char *ns, const char **xml, size_t *xml_len, char **owned);
int sdns_segment_xml(unsigned compr, const unsigned char *data, size_t len, const char *ns, const char **xml, size_t *xml_len, char **owned);

#endif
