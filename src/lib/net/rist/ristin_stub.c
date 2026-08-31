/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/helper/log.h"

#include "ristin.h"

ristin_t *ristin_open(const ristin_cfg_t *cfg) {
  (void)cfg;
  log_line("rist: this build has no librist support, rist:// input unavailable");
  return NULL;
}

int ristin_fd(const ristin_t *r) {
  (void)r;
  return -1;
}

void ristin_close(ristin_t *r) { (void)r; }
