/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/log.h"

#include "srtout.h"

/* built without libsrt: srt:// output always fails cleanly */
srtout_t *srtout_open(const srtout_cfg_t *cfg) {
  (void)cfg;
  log_line("srt: this build has no libsrt support, srt:// output unavailable");
  return NULL;
}

void srtout_service(srtout_t *r, srtout_status_t *out) {
  (void)r;
  out->connected = 0;
}

void srtout_write(srtout_t *r, const unsigned char *buf, size_t n) {
  (void)r;
  (void)buf;
  (void)n;
}

void srtout_close(srtout_t *r) { (void)r; }
