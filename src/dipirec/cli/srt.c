/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"

#include "priv.h"

int rec_parse_pbkeylen_opt(const char *val, int *out, const char *optname) {
  char *end;
  unsigned long v = strtoul(val, &end, 10);
  if (*end != '\0' || (v != 16 && v != 24 && v != 32)) {
    argerr("invalid %s: %s (16|24|32)", optname, val);
    return -1;
  }
  *out = (int)v;
  return 0;
}

int rec_validate_srt_passphrase(const char *passphrase, int pbkeylen, const char *suffix) {
  if (passphrase[0] && (strlen(passphrase) < 10 || strlen(passphrase) > 79)) {
    argerr("--srt-passphrase%s must be 10..79 characters", suffix);
    return -1;
  }
  if (pbkeylen && !passphrase[0]) {
    argerr("--srt-pbkeylen%s requires --srt-passphrase%s", suffix, suffix);
    return -1;
  }
  return 0;
}

