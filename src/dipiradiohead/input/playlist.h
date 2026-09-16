/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_INPUT_PLAYLIST_H
#define DIPIRADIOHEAD_INPUT_PLAYLIST_H

#include <stddef.h>

#include "lib/net/httpclient/httpclient.h"

/* sniffs 'body' for M3U/PLS syntax; on match fills 'url' (cap n) treats body as audio */
int playlist_extract(const unsigned char *body, size_t len, const http_url_t *base, char *url, size_t n);

/* 1: HLS media playlist (EXTM3U+EXTINF), else 0 */
int playlist_is_hls_media(const unsigned char *body, size_t len);

char *playlist_skip_blank(char *p);

/* splits *cursor in place on '\n', NUL-terminated, NULL at end */
char *playlist_next_line(char **cursor);

int playlist_resolve_relative(const http_url_t *base, const char *ref, char *out, size_t n);

#endif
