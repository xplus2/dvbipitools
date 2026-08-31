/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_NET_SEND_RESULT_H
#define LIB_NET_SEND_RESULT_H

#include "lib/helper/log.h"

/* output failures aren't fatal, log only on failure/recovery edge, keep retrying every batch */
static inline void note_send_result(int ok, int *had_error, unsigned long long *errors, const char *label) {
  if (!ok) {
    (*errors)++;
    if (!*had_error) {
      log_line("%s output: send failed, will keep retrying", label);
      *had_error = 1;
    }
  } else if (*had_error) {
    log_line("%s output: recovered", label);
    *had_error = 0;
  }
}

#endif
