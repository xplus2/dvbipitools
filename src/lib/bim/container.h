/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_CONTAINER_H
#define DVBIPITOOLS_LIB_BIM_CONTAINER_H

#include <stddef.h>
#include <stdint.h>

#include "index.h"

/* TS 102 822-3-2 4.5, 4.6 containers, TS 102 323 9.3 DVB BiM fragment encapsulation */

/* fragment_reference_format, TS 102 822-3-2 table 6, TS 102 323 table 52 */
#define BIM_FRAGREF_BIM 0x00
#define BIM_FRAGREF_DVB_BIM 0xE1

/* Encapsulation entry: fragment ref, id+version. str_off only in DVB BiM (table 53) */
typedef struct {
  uint32_t au_off; /* BiM_fragment_ptr */
  uint32_t str_off; /* external_string_buffer_ptr */
  uint32_t frag_id;
  uint8_t version;
} bim_fragref_t;

/* TS 102 822-3-2 4.5.2.1 container of st in order. *out malloc'd. 0 ok, -1 oom or 24 bit overflow */
int bim_container_build_structs(const bim_struct_t *st, int n, unsigned char **out, size_t *out_len);

/* last structure of type/id wins. 0 found, 1 absent, -1 malformed table */
int bim_container_find(const unsigned char *buf, size_t len, unsigned want_type, unsigned want_id, const unsigned char **body, size_t *body_len);

/* container: Encapsulation structure of format (BIM_FRAGREF_*) if nrefs > 0, Data Repository structs
   (binary access unit, strings, none if string_repo NULL). *out malloc'd. 0 ok, -1 oom, size overflow,
   pointer over 16 bits, unknown format */
int bim_container_build(unsigned format, const unsigned char *access_unit, size_t au_len, const unsigned char *string_repo, size_t sr_len, const bim_fragref_t *refs, int nrefs, unsigned char **out, size_t *out_len);

/* au and sr point into buf, sr NULL without string repository. 0 ok, -1 malformed or no binary repository */
int bim_container_parse(const unsigned char *buf, size_t len, const unsigned char **au, size_t *au_len, const unsigned char **sr, size_t *sr_len);

/* Encapsulation entries, *format: fragment_reference_format. *refs malloc'd, NULL with *nrefs 0 if absent or other format.
   0 ok, -1 malformed or oom */
int bim_container_parse_refs(const unsigned char *buf, size_t len, unsigned *format, bim_fragref_t **refs, int *nrefs);

#endif
