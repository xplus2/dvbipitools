/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_INPUT_ID3_H
#define DIPIRADIOHEAD_INPUT_ID3_H

#include <stddef.h>

typedef struct id3 id3_t;

typedef void (*id3_meta_cb)(void *ctx, const char *artist, const char *title);

id3_t *id3_new(id3_meta_cb cb, void *ctx);
void id3_free(id3_t *c);

/* caller must only probe at a frame boundary, never mid-frame (avoids false positives on audio bytes) */
int id3_is_tag(const unsigned char *p, size_t avail);

/* full on-disk tag size (header + body + optional footer). 0 if avail < 10 */
size_t id3_tag_size(const unsigned char *p, size_t avail);

/* parses TIT2/TPE1 out of a complete tag at p[0..taglen); calls back on change */
void id3_consume(id3_t *c, const unsigned char *p, size_t taglen);

#endif
