/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_DEMUX_CRC32_H
#define DIPIREC_DEMUX_CRC32_H

#include <stddef.h>
#include <stdint.h>

/* MPEG-2 CRC32. section incl. own CRC -> 0 */
uint32_t crc32_mpeg(const unsigned char *data, size_t len);

/* reflected CRC-32: zlib/gzip/PNG (poly 0xEDB88320) or Castagnoli (poly 0x82F63B78) */
uint32_t crc32_zlib(int castagnoli, const unsigned char *data, size_t len);

#endif
