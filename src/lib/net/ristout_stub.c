/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "../log.h"
#include "ristout.h"

/* built without librist: rist:// output always fails cleanly */
ristout_t *ristout_open(const ristout_cfg_t *cfg) {
  (void)cfg;
  log_line("rist: this build has no librist support, rist:// output unavailable");
  return NULL;
}

int ristout_write(ristout_t *r, const unsigned char *buf, size_t n) {
  (void)r;
  (void)buf;
  (void)n;
  return -1;
}

void ristout_close(ristout_t *r) { (void)r; }
