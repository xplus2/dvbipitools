/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "priv.h"

int rec_validate_srt_passphrase(const char *passphrase, int pbkeylen, const char *suffix) {
  if (passphrase[0] && (strlen(passphrase) < 10 || strlen(passphrase) > 79)) {
    argerr("CS000560", "--srt-passphrase%s must be 10..79 characters", suffix);
    return -1;
  }
  if (pbkeylen && !passphrase[0]) {
    argerr("CS000561", "--srt-pbkeylen%s requires --srt-passphrase%s", suffix, suffix);
    return -1;
  }
  return 0;
}

