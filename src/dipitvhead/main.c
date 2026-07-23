/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "args.h"
#include "lib/log.h"
#include "lib/signal.h"
#include "tvhead.h"
#include "version.h"

/* banner prints before parsing: --color read early */
static log_color_t color_prescan(int argc, char **argv) {
  int i;
  for (i = 1; i < argc; i++) {
    const char *v = NULL;
    if (!strcmp(argv[i], "--color") && i + 1 < argc)
      v = argv[i + 1];
    else if (!strncmp(argv[i], "--color=", 8))
      v = argv[i] + 8;
    if (!v)
      continue;
    if (!strcmp(v, "always"))
      return LOG_COLOR_ALWAYS;
    if (!strcmp(v, "never"))
      return LOG_COLOR_NEVER;
  }
  return LOG_COLOR_AUTO;
}

int main(int argc, char **argv) {
  config_t cfg;
  char src[600], mcast[80];
  args_status_t st;

  log_set_color(color_prescan(argc, argv));
  log_line_ansi("\e[1m%s\e[0m \e[0;32mv%s\e[0m \e[0;37m%s\e[0m \e[0;37m%s\e[0m \e[0;34m%s\e[0m", TOOL_NAME, TOOL_VERSION, BUILD_ARCH, BUILD_TYPE, BUILD_LINK);
  st = args_parse(argc, argv, &cfg);
  if (st == ARGS_OK)
    log_set_color((log_color_t)cfg.color_mode);
  if (st == ARGS_HELP)
    return 0;
  if (st == ARGS_ERR) {
    fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME);
    return 2;
  }

  source_describe(&cfg.input, src, sizeof src);
  mcast_describe(&cfg, mcast, sizeof mcast);
  log_line_ansi("\e[1mi:\e[0m\e[0;37m%s\e[0m \e[1mm:\e[0m\e[0;37m%s\e[0m \e[1mrtp:\e[0m\e[0;37m%s\e[0m", src, mcast, cfg.rtp ? "yes" : "no");

  signals_install();

  return tvhead_run(&cfg);
}
