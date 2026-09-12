/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "fileutil.h"

FILE *fileutil_open_std(const char *path, const char *mode) {
  if (strcmp(path, "-") == 0) return mode[0] == 'r' ? stdin : stdout;
  return fopen(path, mode);
}
