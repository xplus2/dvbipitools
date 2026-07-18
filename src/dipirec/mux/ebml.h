/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_MUX_EBML_H
#define DIPIREC_MUX_EBML_H

#include <stddef.h>
#include <stdint.h>

/* growable element build buffer */
typedef struct {
    unsigned char *p;
    size_t len, cap;
    int err;                    /* alloc failed */
} ebuf_t;

void ebuf_free(ebuf_t *b);
void eb_bytes(ebuf_t *b, const void *data, size_t n);

/* primitives; id carries length marker */
void eb_id(ebuf_t *b, uint32_t id);
void eb_size(ebuf_t *b, uint64_t size);

void eb_uint(ebuf_t *b, uint32_t id, uint64_t val);
void eb_str(ebuf_t *b, uint32_t id, const char *s);
void eb_bin(ebuf_t *b, uint32_t id, const void *data, size_t n);
void eb_float(ebuf_t *b, uint32_t id, double val);

/* wrap child as master id into parent; frees child */
void eb_master(ebuf_t *parent, uint32_t id, ebuf_t *child);

#endif
