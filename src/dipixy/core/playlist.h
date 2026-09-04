/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_CORE_PLAYLIST_H
#define DIPIXY_CORE_PLAYLIST_H

#include <stddef.h>

#include "../args.h"
#include "../ts/channels/channels.h"
#include "../ts/pidfilter.h"
#include "route.h"

typedef enum { PLAYLIST_M3U, PLAYLIST_XSPF } playlist_type_t;

int playlist_path_parse(const char *path, route_fmt_t *fmt, playlist_type_t *ptype);
int playlist_fmt_disabled(const config_t *cfg, route_fmt_t fmt);
int playlist_query_has_flag(const char *query, const char *name);

/* *out malloc'd via open_memstream, caller frees */
int playlist_render(const config_t *cfg, const channels_t *ch, int is_tls, const char *host_hdr, const char *query, const pid_filter_t *filter,
                    route_fmt_t fmt, playlist_type_t ptype, char **out, size_t *out_len);

#endif
