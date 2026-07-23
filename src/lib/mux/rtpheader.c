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
  out[0] = 0x80; /* V=2, P=0, X=0, CC=0 */
  out[1] = 0x21; /* M=0, PT=33 (MP2T) */
  out[2] = (unsigned char)(r->seq >> 8);
  out[3] = (unsigned char)r->seq;
  r->seq++;
  out[4] = (unsigned char)(pts_90k >> 24);
  out[5] = (unsigned char)(pts_90k >> 16);
  out[6] = (unsigned char)(pts_90k >> 8);
  out[7] = (unsigned char)pts_90k;
  out[8] = (unsigned char)(r->ssrc >> 24);
  out[9] = (unsigned char)(r->ssrc >> 16);
  out[10] = (unsigned char)(r->ssrc >> 8);
  out[11] = (unsigned char)r->ssrc;
  return 12;
}
