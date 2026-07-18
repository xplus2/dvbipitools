/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "rtp.h"

size_t rtp_payload_offset(const unsigned char *p, size_t len) {
  size_t off;
  unsigned cc;

  if (len < 12 || (p[0] >> 6) != 2) /* need RTP version 2 */
    return 0;
  cc = p[0] & 0x0F;
  off = 12 + (size_t)cc * 4;
  if (p[0] & 0x10) { /* extension header */
    if (len < off + 4)
      return 0;
    off += 4 + (size_t)(((unsigned)p[off + 2] << 8) | p[off + 3]) * 4;
  }
  if (off >= len || p[off] != 0x47) /* payload must be TS */
    return 0;
  return off;
}
