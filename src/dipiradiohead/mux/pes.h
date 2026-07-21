/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_MUX_PES_H
#define DIPIRADIOHEAD_MUX_PES_H

#include <stddef.h>
#include <stdint.h>

/* wraps one ES frame (PTS only, no DTS) into a PES packet; 0 on overflow or frame_len > 65527 */
size_t pes_build(uint64_t pts_90k, const unsigned char *frame, size_t frame_len, unsigned char *out, size_t cap);

#endif
