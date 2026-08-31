/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_CAS_CW_GEN_H
#define DVBIPITOOLS_LIB_CAS_CW_GEN_H

#include <errno.h>
#include <string.h>
#include <sys/random.h>
#include <sys/types.h>

#include "../helper/log.h"

/* fills out[0..len) via getrandom(), retrying on EINTR. 0 ok, -1 failed (logged as "tag: getrandom: ...") */
static inline int cw_gen(unsigned char *out, size_t len, const char *tag) {
  size_t got = 0;
  while (got < len) {
    ssize_t n = getrandom(out + got, len - got, 0);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      log_line("%s: getrandom: %s", tag, strerror(errno));
      return -1;
    }
    got += (size_t)n;
  }
  return 0;
}

#endif
