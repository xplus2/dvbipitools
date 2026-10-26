/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_INDEX_H
#define DVBIPITOOLS_LIB_BIM_INDEX_H

#include <stddef.h>
#include <stdint.h>

#include "strrepo.h"

/* TS 102 822-3-2 4.5.2.1 table 4 */
#define BIM_STRUCT_ENCAPSULATION 0x01
#define BIM_STRUCT_DATA_REPOSITORY 0x02
#define BIM_STRUCT_INDEX_LIST 0x03
#define BIM_STRUCT_INDEX 0x04
#define BIM_STRUCT_SUB_INDEX 0x05
/* TS 102 323 table 104 */
#define BIM_STRUCT_TYPE_LIST 0x80
#define BIM_STRUCT_NS_MAP 0x81
/* structure_id of a structure without instances */
#define BIM_STRUCT_ID_UNUSED 0xFF
/* table 5, structure_id for structure_type=0x02 */
#define BIM_DATAREPO_STRINGS 0x00
#define BIM_DATAREPO_BINARY 0x01
/* TS 102 323 table 52: fragment and string buffer pointers are 16 bit */
#define BIM_REPO_MAX 0xFFFFu

/* index list container, TS 102 323 9.2.2 */
#define BIM_INDEX_LIST_CONTAINER 0x0000

/* TS 102 323 9.5.1 profiled indices on fragments we carry */
enum { BIM_INDEX_PI_CRID, BIM_INDEX_PI_TITLE, BIM_INDEX_SCHED_TIME, BIM_INDEX_SCHED_TITLE, BIM_INDEX_GI_CRID, BIM_INDEX_GI_TITLE, BIM_INDEX_KINDS };
#define BIM_INDEX_NAMES "pi-crid|pi-title|schedule-time|schedule-title|gi-crid|gi-title"

/* comma list of BIM_INDEX_NAMES -> bit per kind. 0 ok, -1 unknown or empty name */
int bim_index_parse_names(const char *list, unsigned *mask);

typedef struct {
  uint8_t type;
  uint8_t id;
  unsigned char *body; /* heap */
  size_t len;
} bim_struct_t;

typedef struct {
  uint16_t id;
  bim_struct_t *structs;
  int nstructs;
} bim_index_cont_t;

/* key[1] only for BIM_INDEX_SCHED_TIME (serviceIDRef), key[0] end there. strings borrowed */
typedef struct {
  const char *key[2];
  uint16_t container;
  uint32_t fragment;
} bim_index_entry_t;

typedef struct {
  bim_index_entry_t *e;
  int n;
  int cap;
} bim_index_input_t;

/* 0 ok, -1 oom */
int bim_index_add(bim_index_input_t *in, const char *key0, const char *key1, uint16_t container, uint32_t fragment);
void bim_index_input_free(bim_index_input_t *in);

/* kinds in mask from in[kind] (entries reordered). conts[0]: index list container, sub-index containers
   ids first_id.., each with string repository below 64 KiB. 0 ok, -1 oom, -2 index list strings above 64 KiB, -3 ids exhausted */
int bim_index_build(unsigned mask, bim_index_input_t in[BIM_INDEX_KINDS], uint16_t first_id, bim_index_cont_t **conts, int *nconts);
void bim_index_conts_free(bim_index_cont_t *conts, int n);

/* structure of a received container. 0 found, -1 absent */
typedef int (*bim_index_find_fn)(void *ctx, unsigned container, unsigned type, unsigned id, const unsigned char **body, size_t *len);
/* 1 fragment present in data container */
typedef int (*bim_index_has_fn)(void *ctx, unsigned container, uint32_t fragment);

typedef struct {
  int indexes;     /* profiled entries in index list */
  int ignored;     /* other entries, TS 102 323 9.5.1.3 */
  int entries;     /* leaf entries checked */
  int missing;     /* index or sub-index structure not received */
  int malformed;
  int misordered;
  int unresolved;  /* locator to fragment not received */
} bim_index_report_t;

/* walks index list of container 0x0000. 0 checked, -1 no index list */
int bim_index_check(bim_index_find_fn find, bim_index_has_fn has, void *ctx, bim_index_report_t *rep);

/* TS 102 323 9.5.2.2 type list structure entry. xpath only for fragment_type 0xFFFF */
typedef struct {
  unsigned fragment_type;
  const char *xpath;
  const uint16_t *containers;
  int ncontainers;
} bim_typelist_entry_t;

/* type list body (BIM_STRUCT_TYPE_LIST, id 0), container 0. xpath strings in sw, 16 bit offsets.
   *out mallocd. 0 ok, -1 oom, entry too long, offset over 16 bit */
int bim_typelist_build(const bim_typelist_entry_t *e, int n, strrepo_writer_t *sw, unsigned char **out, size_t *out_len);
/* entries of received body, xpath in string repository (points into strings), containers copied.
   *e mallocd, bim_typelist_free. 0 ok, -1 malformed or oom */
int bim_typelist_parse(const unsigned char *body, size_t len, const unsigned char *strings, size_t slen, bim_typelist_entry_t **e, int *n);
void bim_typelist_free(bim_typelist_entry_t *e, int n);

/* 9.5.2.3 namespace map structure (type BIM_STRUCT_NS_MAP, id 0). *out mallocd. 0 ok, -1 oom or field above 255 bytes */
int bim_nsmap_build(const char *const *prefix, const char *const *ns, int n, unsigned char **out, size_t *out_len);
/* *prefix, *ns mallocd arrays of mallocd strings, bim_nsmap_free. 0 ok, -1 malformed or oom */
int bim_nsmap_parse(const unsigned char *body, size_t len, char ***prefix, char ***ns, int *n);
void bim_nsmap_free(char **prefix, char **ns, int n);

#endif
