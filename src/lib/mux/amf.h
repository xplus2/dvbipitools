/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_AMF_H
#define DVBIPITOOLS_LIB_MUX_AMF_H

#include <stddef.h>

#include "ebml.h"

/* encode: appends to ebuf_t (ebml.c's generic growable buffer) */
void amf_number(ebuf_t *b, double v);
void amf_boolean(ebuf_t *b, int v);
void amf_string(ebuf_t *b, const char *s); /* short string, len clamped to 65535 */
void amf_null(ebuf_t *b);
void amf_object_start(ebuf_t *b);
void amf_object_key(ebuf_t *b, const char *key); /* length-prefixed key, no type marker */
void amf_object_end(ebuf_t *b);
void amf_ecma_array_start(ebuf_t *b, unsigned approx_count);
void amf_ecma_array_end(ebuf_t *b);

/* decode: each takes a value already positioned at its type marker, returns
   pointer past it or NULL if truncated/wrong marker. RTMP rep parsing only. */
const unsigned char *amf_read_number(const unsigned char *p, const unsigned char *end, double *v);
const unsigned char *amf_read_string(const unsigned char *p, const unsigned char *end, char *out, size_t cap);
/* any single value, recurses into object/ecma/strict-array bodies */
const unsigned char *amf_skip_value(const unsigned char *p, const unsigned char *end);

/* p at AMF_T_OBJECT marker, finds string key in body. 1 found, 0 absent, -1 malformed */
int amf_object_find_string(const unsigned char *p, const unsigned char *end, const char *key, char *out, size_t cap);

#endif
