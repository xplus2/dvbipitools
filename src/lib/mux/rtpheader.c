/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include "../sys/ioutil.h"
#include "rtpheader.h"

struct rtpheader {
  uint16_t seq;
  uint32_t ssrc;
};

rtpheader_t *rtpheader_new(void) {
  rtpheader_t *r = calloc(1, sizeof *r);
  if (!r)
    return NULL;
  r->seq = (uint16_t)rand_seed32();
  r->ssrc = rand_seed32();
  return r;
}

void rtpheader_free(rtpheader_t *r) { free(r); }

size_t rtpheader_build(rtpheader_t *r, uint32_t pts_90k, unsigned char *out, size_t cap) {
  if (cap < 12)
    return 0;
  rtp_write_header(out, 0x21, r->seq++, pts_90k, r->ssrc); /* PT=33 (MP2T) */
  return 12;
}
