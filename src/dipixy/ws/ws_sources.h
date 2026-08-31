/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_WS_SOURCES_H
#define DIPIXY_WS_SOURCES_H

#include "../args.h"
#include "../ts/channels/channels.h"

/* sources.snapshot json, every configured -i in command-line order. thread-local, valid until next call, no free. 0 ok, -1 OOM */
int ws_sources_build_snapshot(const config_t *cfg, const channels_t *channels, char **out);

/* sources.update json for one reloaded list. thread-local, valid until next call, no free. 0 ok, -1 OOM */
int ws_sources_build_update(const channels_t *channels, const source_def_t *src, unsigned list_num, char **out);

#endif
