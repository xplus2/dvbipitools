/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_TOOLMAIN_H
#define LIB_TOOLMAIN_H

/* startup banner: name, version, arch, build type, link mode */
void toolmain_print_banner(const char *tool_name, const char *tool_version, const char *build_arch, const char *build_type, const char *build_link);

/* daemon(1,1) if requested, tool_name tags failure log line. 0 ok, -1 failed (already logged) */
int toolmain_daemonize(int daemonize, const char *tool_name);

#endif
