/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "args.h"
#include "lib/helper/log.h"
#include "lib/helper/signal.h"
#include "scan.h"
#include "version.h"

/* banner prints before possible arg errors: --color read early */
int main(int argc, char **argv) {
  config_t cfg;
  args_status_t st;
  FILE *out;
  int rc;
  log_set_color(log_color_prescan(argc, argv));
  log_line_ansi("\e[1m%s\e[0m \e[0;32mv%s\e[0m \e[0;37m%s\e[0m \e[0;37m%s\e[0m \e[0;34m%s\e[0m", TOOL_NAME, TOOL_VERSION, BUILD_ARCH, BUILD_TYPE, BUILD_LINK);
  st = args_parse(argc, argv, &cfg);
  if (st == ARGS_NOARGS) {
    fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME);
    return 0;
  }
  if (st == ARGS_HELP)
    return 0;
  if (st == ARGS_ERR) {
    fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME);
    return 2;
  }
  log_set_color((log_color_t)cfg.color_mode);

  if (cfg.out_path && strcmp(cfg.out_path, "-") != 0) {
    out = fopen(cfg.out_path, "w");
    if (!out) {
      log_line("open %s: %s", cfg.out_path, strerror(errno));
      return 1;
    }
  } else {
    out = stdout;
  }
  setvbuf(out, NULL, _IOLBF, 0);

  signals_install();
  rc = scan_run(&cfg, out);

  if (out != stdout)
    fclose(out);
  return rc;
}
