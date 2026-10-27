/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "cli/args.h"
#include "lib/helper/log.h"
#include "lib/sys/signal.h"
#include "lib/helper/toolmain.h"
#include "scan.h"
#include "version.h"

/* banner prints before possible arg errors: --color read early */
int main(int argc, char **argv) {
  config_t cfg;
  args_status_t st;
  FILE *out;
  int rc;
  TOOLMAIN_VERSION_EXIT(argc, argv);
  log_set_color(log_color_prescan(argc, argv));
  toolmain_print_banner(TOOL_NAME, TOOL_VERSION, BUILD_ARCH, BUILD_TYPE, BUILD_LINK);
  st = args_parse(argc, argv, &cfg);
  if (st == ARGS_NOARGS) {
    fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME);
    return 0;
  }
  if (st == ARGS_HELP) return 0;
  if (st == ARGS_ERR) {
    fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME);
    return 2;
  }
  log_set_color((log_color_t)cfg.color_mode);
  if (cfg.out_path && strcmp(cfg.out_path, "-") != 0) {
    out = fopen(cfg.out_path, "w");
    if (!out) {
      log_code("SS100108", "open %s: %s", cfg.out_path, strerror(errno));
      return 1;
    }
  } else {
    out = stdout;
  }
  setvbuf(out, NULL, _IOLBF, 0);
  signals_install_oneshot();
  rc = scan_run(&cfg, out);
  {
    int failed = fflush(out) != 0 || ferror(out);
    if (out != stdout) failed |= fclose(out) != 0;
    if (failed && rc == 0) {
      log_code("SE100102", "error writing %s", out == stdout ? "stdout" : cfg.out_path);
      rc = 1;
    }
  }
  return rc;
}
