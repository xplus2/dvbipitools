/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "rtpheader.h"

struct rtpheader {
  uint16_t seq;
  uint32_t ssrc;
};

rtpheader_t *rtpheader_new(void) {
  rtpheader_t *r = calloc(1, sizeof *r);
  if (!r)
    return NULL;
  srand((unsigned)(time(NULL) ^ getpid()));
  r->seq = (uint16_t)rand();
  r->ssrc = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
  return r;
}

void rtpheader_free(rtpheader_t *r) { free(r); }

size_t rtpheader_build(rtpheader_t *r, uint32_t pts_90k, unsigned char *out, size_t cap) {
  if (cap < 12)
    return 0;
  rtp_write_header(out, 0x21, r->seq++, pts_90k, r->ssrc); /* PT=33 (MP2T) */
  return 12;
}
