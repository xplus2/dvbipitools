/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_INPUT_ICY_H
#define DIPIRADIOHEAD_INPUT_ICY_H

#include <stddef.h>

typedef struct icy icy_t;

typedef void (*icy_meta_cb)(void *ctx, const char *artist, const char *title);

/* metaint: bytes of audio between metadata blocks, from icy-metaint response header */
icy_t *icy_new(size_t metaint, icy_meta_cb cb, void *ctx);
void icy_free(icy_t *c);

/* strips ICY metadata blocks from 'in' (fully consumed) into 'out' (cap >= inlen); returns bytes written, cb on title change */
size_t icy_feed(icy_t *c, const unsigned char *in, size_t inlen, unsigned char *out, size_t cap);

#endif
