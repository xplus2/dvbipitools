/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_XSD_XSD_H
#define DVBIPITOOLS_LIB_BIM_XSD_XSD_H

#include <stddef.h>
#include <stdint.h>

#include "lib/bim/codec.h"
#include "lib/bim/decoderinit.h"
#include "lib/helper/xml_util.h"

/* XML Schema Part 1 components (W3C 2001) as BiM coding needs them */

#define XSD_NS "http://www.w3.org/2001/XMLSchema"
#define XSD_UNBOUNDED UINT64_MAX

/* ISO 23001-1 6.7.8.3 schema for encoding schema components */
#define XSD_ENCODING_NS "urn:mpeg:mpeg7:systems:encodingschema:amd1:2004"
extern const char xsd_encoding_schema[];

typedef struct xsd_type xsd_type_t;
typedef struct xsd_elem xsd_elem_t;
typedef struct xsd_attr xsd_attr_t;
typedef struct xsd_group xsd_group_t;
typedef struct xsd_agroup xsd_agroup_t;
typedef struct xsd_set xsd_set_t;

/* ns NULL: no namespace */
typedef struct {
  const char *ns;
  const char *name;
} xsd_qname_t;

typedef enum { XSD_WC_ANY, XSD_WC_NOT, XSD_WC_LIST } xsd_wc_kind_t;
typedef enum { XSD_PC_STRICT, XSD_PC_LAX, XSD_PC_SKIP } xsd_process_t;

/* wildcard. NOT: all but ns[0] (NULL: absent). LIST: ns[] (NULL entry: absent) */
typedef struct {
  xsd_wc_kind_t kind;
  const char **ns;
  int nns;
  xsd_process_t process;
} xsd_wildcard_t;

typedef enum { XSD_P_ELEM, XSD_P_GROUP, XSD_P_SEQ, XSD_P_CHOICE, XSD_P_ALL, XSD_P_ANY } xsd_pkind_t;

typedef struct xsd_particle {
  xsd_pkind_t kind;
  uint64_t min;
  uint64_t max; /* XSD_UNBOUNDED */
  xsd_elem_t *elem; /* ELEM: local declaration or global by ref */
  xsd_group_t *group; /* GROUP */
  struct xsd_particle *kids; /* SEQ, CHOICE, ALL */
  int nkids;
  xsd_wildcard_t *wc; /* ANY */
  xsd_qname_t ref; /* ELEM or GROUP by ref until resolved */
} xsd_particle_t;

typedef enum { XSD_USE_OPTIONAL, XSD_USE_REQUIRED, XSD_USE_PROHIBITED } xsd_use_t;

struct xsd_attr {
  xsd_qname_t q;
  xsd_type_t *type;
  const char *dflt;
  const char *fixed;
  int global;
  xsd_qname_t type_ref;
};

typedef struct {
  xsd_attr_t *attr; /* local declaration or global by ref */
  xsd_use_t use;
  const char *dflt; /* use-level, overrides declaration */
  const char *fixed;
  xsd_qname_t ref;
} xsd_attr_use_t;

/* attribute uses, attributeGroup refs and attribute wildcard of a type or group */
typedef struct {
  xsd_attr_use_t *uses;
  int nuses;
  xsd_agroup_t **groups;
  xsd_qname_t *group_refs;
  int ngroups;
  xsd_wildcard_t *wc;
} xsd_attrs_t;

struct xsd_agroup {
  xsd_qname_t q;
  xsd_attrs_t attrs;
};

struct xsd_group {
  xsd_qname_t q;
  xsd_particle_t *particle; /* SEQ, CHOICE or ALL */
};

struct xsd_elem {
  xsd_qname_t q;
  xsd_type_t *type;
  const char *dflt;
  const char *fixed;
  int global;
  int nillable;
  int abstract;
  xsd_elem_t *subst_head;
  xsd_qname_t type_ref;
  xsd_qname_t subst_ref;
};

typedef enum {
  XSD_F_LENGTH,
  XSD_F_MINLENGTH,
  XSD_F_MAXLENGTH,
  XSD_F_PATTERN,
  XSD_F_ENUMERATION,
  XSD_F_WHITESPACE,
  XSD_F_MAXINCLUSIVE,
  XSD_F_MAXEXCLUSIVE,
  XSD_F_MININCLUSIVE,
  XSD_F_MINEXCLUSIVE,
  XSD_F_TOTALDIGITS,
  XSD_F_FRACTIONDIGITS,
  XSD_F_COUNT
} xsd_facet_kind_t;

typedef struct {
  xsd_facet_kind_t kind;
  const char *value;
} xsd_facet_t;

typedef enum { XSD_SIMPLE, XSD_COMPLEX } xsd_kind_t;
typedef enum { XSD_D_NONE, XSD_D_RESTRICTION, XSD_D_EXTENSION, XSD_D_LIST, XSD_D_UNION } xsd_deriv_t;
typedef enum { XSD_C_EMPTY, XSD_C_SIMPLE, XSD_C_ELEMENTS } xsd_content_t;

/* own parts only, inherited ones via base. q.name NULL: anonymous */
struct xsd_type {
  xsd_qname_t q;
  xsd_kind_t kind;
  xsd_deriv_t deriv;
  xsd_type_t *base;
  int builtin;
  int abstract;
  int mixed;
  /* simple: restriction facets, list item, union members */
  xsd_facet_t *facets;
  int nfacets;
  xsd_type_t *item;
  xsd_type_t **members;
  int nmembers;
  /* complex */
  xsd_content_t content;
  xsd_type_t *simple; /* XSD_C_SIMPLE: inline simpleType of a simpleContent restriction */
  xsd_particle_t *particle; /* own content particle, NULL none */
  xsd_attrs_t attrs;
  /* until resolved */
  xsd_qname_t base_ref;
  xsd_qname_t item_ref;
  xsd_qname_t *member_refs;
  int nmember_refs;
};

/* schema documents of one set, components resolved across them. err: message on failure */
xsd_set_t *xsd_set_new(void);
void xsd_set_free(xsd_set_t *s);
const char *xsd_set_error(const xsd_set_t *s);
/* code of xsd_set_error per doc/error_codes.md */
const char *xsd_set_error_code(const xsd_set_t *s);

/* adds components of one schema document, set keeps doc. path for messages.
   0 ok, -1 invalid schema, -2 out of memory */
int xsd_set_add_doc(xsd_set_t *s, xml_doc_t *doc, const char *path);
/* as add_doc for include/redefine into tns (XML Schema Part 1 4.2.1, chameleon if doc has none) */
int xsd_set_include_doc(xsd_set_t *s, xml_doc_t *doc, const char *path, const char *tns);

/* import/include/redefine of last added document. kind: 'm' import, 'i' include, 'r' redefine.
   ns: import namespace, else including document's */
typedef struct {
  char kind;
  const char *ns;
  const char *location;
  const struct bim_node *node;
} xsd_ref_t;
int xsd_set_doc_refs(const xsd_set_t *s, const xsd_ref_t **refs);

/* XML Schema Part 1 4.2.2: redefinitions in node (xs:redefine) replace components of redefined document */
int xsd_set_redefine(xsd_set_t *s, const xsd_ref_t *ref, const char *path);

/* zeroed, freed with s. NULL on oom (sticky, xsd_set_error) */
void *xsd_set_alloc(xsd_set_t *s, size_t n);

/* code and message for xsd_set_error, first one kept. returns -1 */
int xsd_set_fail(xsd_set_t *s, const char *code, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

/* binds references across all added documents. 0 ok, -1 unresolved, -2 out of memory */
int xsd_set_resolve(xsd_set_t *s);

/* *.xsd files below search dirs, targetNamespace read once each. One per job, x NULL below: private per call. not thread-safe :( */
typedef struct xsd_index xsd_index_t;
xsd_index_t *xsd_index_new(void);
void xsd_index_free(xsd_index_t *x);

/* file below dirs declaring ns, path in out. 0 found, 1 none, 2 several (out: first), -1 error (xsd_set_error) */
int xsd_find_schema(xsd_set_t *s, xsd_index_t *x, const char *const *dirs, int ndirs, const char *ns, char *out, size_t cap);

/* results of xsd_load_for_ns besides 0, -1 (xsd_set_error) and -2 (out of memory) */
#define XSD_LOAD_NONE 1 /* no schema file for ns, or no file and ns NULL */
#define XSD_LOAD_SEVERAL 2 /* several files for ns */
#define XSD_LOAD_FILES 3 /* several files and ns NULL, or several declare ns. *culprit: second */
#define XSD_LOAD_STAT 4 /* *culprit not readable */

/* schema of ns with imports. paths: files and search dirs, file's dir searched too. One file as is, of several:
   file declaring ns, else search dirs. tns_out (NULL ok): tns of main file, "" none */
int xsd_load_for_ns(xsd_set_t *s, xsd_index_t *x, const char *const *paths, int npaths, const char *ns, const char **culprit, char *tns_out, size_t tns_cap);
/* extra schema: file only if it declares ns. ns required */
int xsd_load_extra(xsd_set_t *s, xsd_index_t *x, const char *const *paths, int npaths, const char *ns, const char **culprit);
/* 1: ns only imported by others, never chunked (xml, XML Schema) */
int xsd_helper_ns(const char *ns);
/* logs a non-zero xsd_load_for_ns result. startup, runtime: tool + type of codes, "BS" "BE" */
void xsd_load_report(const char *startup, const char *runtime, int rc, const char *culprit, const char *ns, const xsd_set_t *s);

/* main schema and all it imports, includes, redefines, resolved. dirs: schema search directories
   (XML Schema Part 1 4.3.2 location hints). 0 ok, -1 error (xsd_set_error), -2 out of memory */
int xsd_set_load(xsd_set_t *s, xsd_index_t *x, const char *path, const char *const *dirs, int ndirs);

const xsd_type_t *xsd_find_type(const xsd_set_t *s, const char *ns, const char *name);
const xsd_elem_t *xsd_find_elem(const xsd_set_t *s, const char *ns, const char *name);
const xsd_attr_t *xsd_find_attr(const xsd_set_t *s, const char *ns, const char *name);

/* global components, in document order */
int xsd_types(const xsd_set_t *s, const xsd_type_t *const **out);
int xsd_elems(const xsd_set_t *s, const xsd_elem_t *const **out);
int xsd_attrs(const xsd_set_t *s, const xsd_attr_t *const **out);

/* pattern facet, XML Schema Part 2 Appendix F. NULL: malformed or unsupported (\p, \P, \w, \W), lives in s */
typedef struct xsd_pattern xsd_pattern_t;
xsd_pattern_t *xsd_pattern_compile(xsd_set_t *s, const char *re);
/* whole value. 1 match, 0 none, -1 step limit. p: xsd_pattern_t, as bim_simple_t.match */
int xsd_pattern_match(const void *p, const char *v);

/* codec for a simple type, derived: also for types derived from it */
typedef struct {
  const char *ns;
  const char *name;
  bim_codec_t codec;
  int derived;
} xsd_codec_map_t;

typedef struct {
  int mpegb; /* 23001-1 rules, 0: 15938-1:2002 */
  int zlib; /* UTF-8 values via Zlib codec, TS 102 822-3-2 4.4.2.4.4 */
  int hier; /* HierarchyBasedSubstitutionCodingFlag */
  const xsd_codec_map_t *codecs; /* before edition defaults */
  int ncodecs;
  unsigned compat; /* BIM_COMPAT_ATTR_LOCAL, BIM_COMPAT_FIXED_ATTR, BIM_COMPAT_LIST_STRING, BIM_COMPAT_M7_FLAGS */
} xsd_compile_opts_t;

/* types: named types depth-first along derivation from anyType, siblings by expanded name (6.6.5.4).
   elems: global elements in selector order (6.6.5.2.3). schema: subtypes in type code order */
typedef struct {
  const bim_type_t *const *types;
  int ntypes;
  const bim_elem_t *const *elems;
  int nelems;
  bim_schema_t schema;
} xsd_compiled_t;

/* resolved set to BiM coding structures, ISO 23001-1 6.6.5, 7.5.2 .. 7.5.4, ISO 15938-1:2002 8.5.
   result and its strings live in s. 0 ok, -1 error (xsd_set_error), -2 out of memory */
int xsd_compile(xsd_set_t *s, const xsd_compile_opts_t *o, const xsd_compiled_t **out);
const bim_type_t *xsd_compiled_type(const xsd_compiled_t *c, const char *ns, const char *name);
const bim_elem_t *xsd_compiled_elem(const xsd_compiled_t *c, const char *ns, const char *name);
/* 1: c defines a type or element of ns */
int xsd_compiled_has_ns(const xsd_compiled_t *c, const char *ns);
/* namespaces in tree that c lacks (not xml, xsi), once each, tree-owned. Count, -1 cap too small */
int xsd_tree_extra_ns(const xsd_compiled_t *c, const bim_node_t *root, const char **ns, size_t cap);
/* resolves xsi:type in cs[0..n-1], first hit wins, sets cast. -1 unknown type (*bad: QName) or unbound prefix */
int xsd_cast_bind(const xsd_compiled_t *const *cs, size_t n, bim_node_t *root, const char **bad);

/* encoding schema in s. 0 ok, -1 error (xsd_set_error), -2 out of memory */
int xsd_encoding_compile(xsd_set_t **s, const xsd_compiled_t **c);

/* BinaryEncodedSchema (6.7.8.2) to encoding schema tree, casts in bim_node_t.cast */
int xsd_suu_decode(const xsd_compiled_t *enc, const unsigned char *bits, uint64_t nbits, bim_node_t **tree, bim_err_t *err);

/* type_codes, elem_codes: TransmittedType/TransmittedElement of partial tables, NULL: position.
   known: compiled imported namespaces and earlier SUUs */
typedef struct {
  const bim_node_t *tree;
  const char *tns;
  const char *const *imported;
  int nimported;
  const xsd_compiled_t *known;
  const uint64_t *type_codes;
  size_t ntype_codes;
  const uint64_t *elem_codes;
  size_t nelem_codes;
} xsd_suu_t;

/* additional schema source: global components of tns in set, imported in NamespaceID order,
   known: compiled imported namespaces (IDs by their order) */
typedef struct {
  const xsd_set_t *set;
  const char *tns;
  const char *const *imported;
  int nimported;
  const xsd_compiled_t *known;
} xsd_suu_src_t;

/* encoding schema tree (6.7.8.3) of tns schema, own component IDs by list position. *tree mallocd */
int xsd_suu_from_set(const xsd_compiled_t *enc, const xsd_suu_src_t *in, bim_node_t **tree, bim_err_t *err);
/* BinaryEncodedSchema (6.7.8.2) of tree, one AddContent FUU */
int xsd_suu_encode(const xsd_compiled_t *enc, const bim_node_t *tree, bitwriter_t *bw, bim_err_t *err);

/* XML Schema document for xsd_set_add_doc, *text mallocd */
int xsd_suu_to_xsd(const xsd_suu_t *in, char **text, bim_err_t *err);

#endif
