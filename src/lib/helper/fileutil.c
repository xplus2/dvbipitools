/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "fileutil.h"

FILE *fileutil_open_std(const char *path, const char *mode) {
  if (strcmp(path, "-") == 0) return mode[0] == 'r' ? stdin : stdout;
  return fopen(path, mode);
}

int fileutil_copy_out(FILE *tmp, const char *path) {
  char buf[16384];
  size_t n;
  FILE *out;
  int rc = 0;
  if (fflush(tmp) || ferror(tmp) || fseek(tmp, 0, SEEK_SET)) return -2;
  out = fileutil_open_std(path, "w");
  if (!out) return -1;
  while ((n = fread(buf, 1, sizeof buf, tmp)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      rc = -2;
      break;
    }
  }
  if (ferror(tmp) || fflush(out) || ferror(out)) rc = -2;
  if (out != stdout && fclose(out)) rc = -2;
  return rc;
}
