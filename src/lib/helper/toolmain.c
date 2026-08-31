/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "toolmain.h"

void toolmain_print_banner(const char *tool_name, const char *tool_version, const char *build_arch, const char *build_type, const char *build_link) {
  log_line_ansi("\e[1m%s\e[0m \e[0;32mv%s\e[0m \e[0;37m%s\e[0m \e[0;37m%s\e[0m \e[0;34m%s\e[0m", tool_name, tool_version, build_arch, build_type, build_link);
}

int toolmain_daemonize(int daemonize, const char *tool_name) {
  if (!daemonize)
    return 0;
  if (daemon(1, 1) != 0) {
    log_line("%s: daemonize failed: %s", tool_name, strerror(errno));
    return -1;
  }
  return 0;
}
