/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_NODEXML_H
#define DVBIPITOOLS_LIB_BIM_NODEXML_H

#include <stddef.h>

#include "codec.h"
#include "lib/helper/xml_util.h"

/* bim_node_t tree as XML text. Namespaces declared on root: own as default, others by prefixes, else declaration in tree,
   else nsN. xsi:type from node->cast */
typedef struct {
  const bim_nsdecl_t *prefixes; /* preferred prefix per namespace URI, prefix "" not used */
  int nprefixes;
  int indent; /* spaces per level, 0: one line */
  int declaration; /* XML declaration first */
} bim_xml_opts_t;

/* 0 ok, -1 out of memory */
int bim_node_write_xml(const bim_node_t *root, const bim_xml_opts_t *o, xml_put_fn put, void *ctx);
/* *out mallocd, NUL terminated. 0 ok, -1 out of memory */
int bim_node_to_xml(const bim_node_t *root, const bim_xml_opts_t *o, char **out, size_t *len);

#endif
