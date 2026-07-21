/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_INPUT_PLAYLIST_H
#define DIPIRADIOHEAD_INPUT_PLAYLIST_H

#include <stddef.h>

/* sniffs 'body' for M3U/PLS syntax; on match fills 'url' (cap n) treats body as audio */
int playlist_extract(const unsigned char *body, size_t len, char *url, size_t n);

#endif
