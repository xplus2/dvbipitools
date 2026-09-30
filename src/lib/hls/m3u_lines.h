/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HLS_M3U_LINES_H
#define DVBIPITOOLS_LIB_HLS_M3U_LINES_H

#include <stddef.h>
#include "lib/net/httpclient/httpclient.h"

char *playlist_skip_blank(char *p);
char *playlist_next_line(char **cursor);
int playlist_resolve_relative(const http_url_t *base, const char *ref, char *out, size_t n);
void playlist_resolve_uri(const http_url_t *base, const char *ref, char *out, size_t n);

#endif
