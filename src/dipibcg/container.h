/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBCG_CONTAINER_H
#define DIPIBCG_CONTAINER_H

#include <stddef.h>

#include "lib/bim/accessunit.h"

/* TS 102 822-3-2 4.5.2.1 table 4 */
#define CONTAINER_STRUCT_ENCAPSULATION 0x01
#define CONTAINER_STRUCT_DATA_REPOSITORY 0x02
/* TS 102 323 table 52: fragment and string buffer pointers are 16 bit */
#define CONTAINER_MAX_REPO 0xFFFFu
/* structure_id of a structure without instances */
#define CONTAINER_ID_UNUSED 0xFF
/* TS 102 323 table 51 */
#define CONTAINER_FRAGREF_DVB_BIM 0xE1
/* table 5, structure_id for structure_type=0x02 */
#define CONTAINER_DATAREPO_STRINGS 0x00
#define CONTAINER_DATAREPO_BINARY 0x01

/* container with an Encapsulation structure (TS 102 323 9.3, fragment ids 1..nrefs ascending, version 0) when
   nrefs > 0, plus Data Repository structures (binary access unit + strings). *out is malloc'd, caller frees.
   0 ok, -1 oom, size field overflow or a ref pointer beyond 16 bits */
int container_build(const unsigned char *access_unit, size_t au_len, const unsigned char *string_repo, size_t sr_len, const accessunit_ref_t *refs, int nrefs, unsigned char **out, size_t *out_len);

/* au and sr point into buf, no copy. 0 ok, -1 malformed or structures not found */
int container_parse(const unsigned char *buf, size_t len, const unsigned char **au, size_t *au_len, const unsigned char **sr, size_t *sr_len);

/* DVB_BiM_fragment_reference entries of the Encapsulation structure. *refs malloc'd (caller frees), NULL with
   *nrefs 0 when there is no usable Encapsulation structure. 0 ok, -1 malformed or oom */
int container_parse_refs(const unsigned char *buf, size_t len, accessunit_ref_t **refs, int *nrefs);

#endif
