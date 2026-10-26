/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_TVAFRAG_H
#define DVBIPITOOLS_LIB_BIM_TVAFRAG_H

#include "xsd/xsd.h"

/* TV-Anytime fragment types, TS 102 822-3-2 table 7 + TS 102 323 table 58 */

/* namespaces behind prefixes of table 7. NULL: schema not used */
typedef struct {
  const char *tva;
  const char *tva2;
  const char *intr;
} bim_tvans_t;

/* root element of fragment type, TVAMain is topmost element */
typedef struct {
  const char *name;
  const char *ns;
  const bim_type_t *type; /* EquivalentStartType */
} bim_tvaroot_t;

/* ns->tva2, ns->intr: extended + interstitial namespaces c codes, NULL none. ns->tva stays */
void bim_tvans_detect(const xsd_compiled_t *c, bim_tvans_t *ns);

/* element name of fragment type code (table 58 and table 7), NULL reserved or user defined */
const char *bim_tvafrag_name(unsigned code);

/* 0 found, -1 reserved, user defined or not in c */
int bim_tvafrag_root(const xsd_compiled_t *c, const bim_tvans_t *ns, unsigned code, bim_tvaroot_t *out);

/* elements from TVAMain down to the root with their types. returns count, -1 none or more than cap */
int bim_tvafrag_steps(const xsd_compiled_t *c, const bim_tvans_t *ns, unsigned code, bim_tvaroot_t *steps, int cap);

/* fragment type of a root element, -1 none */
int bim_tvafrag_code(const xsd_compiled_t *c, const bim_tvans_t *ns, const char *name, const char *elem_ns, const bim_type_t *type);

/* fragment of a TVAMain document */
typedef struct {
  unsigned code;
  bim_node_t *root; /* owned */
} bim_tvapart_t;

/* fragments of doc in document order, copies without nested fragments. containers stay: TVAMain fragment holds
   TS 102 822-3-2 4.3.1.1 tables. ancestor declarations kept on copy, element names schema-owned.
   *out mallocd, bim_tvafrag_free. 0 ok, -1 oom, -2 doc is no TVAMain */
int bim_tvafrag_split(const xsd_compiled_t *c, const bim_tvans_t *ns, const bim_node_t *doc, bim_tvapart_t **out, size_t *n);
void bim_tvafrag_free(bim_tvapart_t *p, size_t n);
/* deep copy of p, *out mallocd, bim_tvafrag_free. 0 ok, -1 oom */
int bim_tvafrag_copy(const bim_tvapart_t *p, size_t n, bim_tvapart_t **out);

/* TVAMain tree of fragments, takes roots (set NULL), creates containers in schema order.
   Type not in c: -3. *out caller-owned. 0 ok, -1 oom */
int bim_tvafrag_join(const xsd_compiled_t *c, const bim_tvans_t *ns, bim_tvapart_t *p, size_t n, bim_node_t **out);

#endif
