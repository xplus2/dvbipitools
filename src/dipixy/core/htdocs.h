/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_HTDOCS_H
#define DIPIXY_HTDOCS_H

#include <stddef.h>

#include "../args.h"

/* generated at build time from htdocs/index.html by gen_htdocs */
extern const char g_htdocs_index_html[];
extern const size_t g_htdocs_index_html_len;

/* loads cfg->status_template if set. logs + keeps embedded default on failure */
void htdocs_template_init(const config_t *cfg);

/* reloads --status-tpl on SIGHUP. call periodically from the reactor tick */
void htdocs_template_reload_check(void);

/* content to serve at GET /: --status-tpl if loaded, else the embedded default */
void htdocs_get(const char **buf, size_t *len);

#endif
