/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "tspacket_write.h"

static void put_pcr(unsigned char *p, uint64_t base33, unsigned ext9) {
  uint64_t b = base33 & 0x1FFFFFFFFULL;
  p[0] = (unsigned char)(b >> 25);
  p[1] = (unsigned char)(b >> 17);
  p[2] = (unsigned char)(b >> 9);
  p[3] = (unsigned char)(b >> 1);
  p[4] = (unsigned char)(((b & 1) << 7) | 0x7E | ((ext9 >> 8) & 1));
  p[5] = (unsigned char)ext9;
}

static void write_packet(unsigned pid, unsigned char *cc, int pusi, const unsigned char *pointer_byte, const unsigned char *payload, size_t payload_len, int with_pcr, uint64_t pcr_90k, size_t pad, ts_packet_cb cb, void *ctx) {
  unsigned char pkt[188];
  size_t pos;

  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pusi ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  *cc = (unsigned char)((*cc + 1) & 0x0F);
  if (with_pcr) {
    unsigned af_len = (unsigned)(7 + pad);
    pkt[3] = (unsigned char)(0x30 | *cc);
    pkt[4] = (unsigned char)af_len;
    pkt[5] = 0x10; /* PCR_flag only */
    put_pcr(pkt + 6, pcr_90k, 0);
    pos = 12;
    memset(pkt + pos, 0xFF, pad);
    pos += pad;
  } else if (pad == 1) {
    pkt[3] = (unsigned char)(0x30 | *cc);
    pkt[4] = 0x00;
    pos = 5;
  } else if (pad > 1) {
    unsigned af_len = (unsigned)(pad - 1);
    pkt[3] = (unsigned char)(0x30 | *cc);
    pkt[4] = (unsigned char)af_len;
    pkt[5] = 0x00;
    memset(pkt + 6, 0xFF, af_len - 1);
    pos = 5 + af_len;
  } else {
    pkt[3] = (unsigned char)(0x10 | *cc);
    pos = 4;
  }

  if (pointer_byte)
    pkt[pos++] = *pointer_byte;
  memcpy(pkt + pos, payload, payload_len);
  cb(ctx, pkt);
}

size_t ts_packet_emit(unsigned pid, unsigned char *cc, const unsigned char *pointer_byte, const unsigned char *data, size_t len, int pcr_first, uint64_t pcr_90k, ts_packet_cb cb, void *ctx) {
  size_t sent = 0, count = 0;
  int first = 1;
  while (first || sent < len) {
    size_t remaining = len - sent;
    size_t ptr_overhead = (first && pointer_byte) ? 1 : 0;
    int with_pcr = first && pcr_first;
    size_t af_fixed = with_pcr ? 8 : 0;
    size_t space = 184 - ptr_overhead - af_fixed;
    size_t take = remaining < space ? remaining : space;
    size_t pad = space - take;
    write_packet(pid, cc, first, first ? pointer_byte : NULL, data + sent, take, with_pcr, pcr_90k, pad, cb, ctx);
    sent += take;
    first = 0;
    count++;
  }
  return count;
}
