/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "cli/args.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/fileutil.h"
#include "lib/helper/log.h"
#include "lib/helper/toolmain.h"
#include "job.h"
#include "version.h"

static int copy_to_output(FILE *tmp, const char *path) {
  int rc = fileutil_copy_out(tmp, path);
  if (rc == -1) log_code("BS100104", TOOL_NAME ": cannot open %s", path);
  if (rc == -2) log_code("BE100102", TOOL_NAME ": error writing %s", path);
  return rc != 0;
}

int main(int argc, char **argv) {
  config_t cfg = {0};
  job_t job;
  FILE *in;
  FILE *tmp;
  int rc;

  TOOLMAIN_STARTUP(argc, argv, &cfg, args_parse);
  if (job_uses_zlib(&cfg) && !ZLIB_BUILT) {
    log_code("BE400317", "zlib support not built in");
    return 1;
  }
  in = fileutil_open_std(cfg.input_path, "r");
  if (!in) {
    log_code("BS000100", TOOL_NAME ": cannot open %s", cfg.input_path);
    return 1;
  }
  tmp = tmpfile();
  if (!tmp) {
    log_code("BS500250", TOOL_NAME ": cannot create temporary file");
    if (in != stdin) fclose(in);
    return 1;
  }
  memset(&job, 0, sizeof job);
  job.cfg = &cfg;
  if (cfg.format == FMT_XML) rc = job_encode(&job, in, tmp) ? 1 : 0;
  else rc = job_decode(&job, in, tmp) ? 1 : 0;
  job_free(&job);
  yamlcfg_strpool_free(cfg.str_pool);

  if (in != stdin) fclose(in);
  if (rc == 0) rc = copy_to_output(tmp, cfg.output_path);
  fclose(tmp);
  return rc;
}
