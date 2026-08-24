/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/log.h"

#include "srtsink.h"

/* built without libsrt: srt:// output always fails cleanly */
srtsink_t *srtsink_open(const srtsink_cfg_t *cfg) {
  (void)cfg;
  log_line("srt: this build has no libsrt support, srt:// output unavailable");
  return NULL;
}

void srtsink_service(srtsink_t *r, srtsink_status_t *out) {
  (void)r;
  out->connected = 0;
}

void srtsink_write(srtsink_t *r, const unsigned char *buf, size_t n) {
  (void)r;
  (void)buf;
  (void)n;
}

void srtsink_close(srtsink_t *r) { (void)r; }
