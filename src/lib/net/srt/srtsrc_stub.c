/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/log.h"

#include "srtsrc.h"

srtsrc_t *srtsrc_open(const srtsrc_cfg_t *cfg) {
  (void)cfg;
  log_line("srt: this build has no libsrt support, srt:// input unavailable");
  return NULL;
}

int srtsrc_fd(const srtsrc_t *r) {
  (void)r;
  return -1;
}

void srtsrc_close(srtsrc_t *r) { (void)r; }
