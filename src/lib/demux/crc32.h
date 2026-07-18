/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_DEMUX_CRC32_H
#define DIPIREC_DEMUX_CRC32_H

#include <stddef.h>
#include <stdint.h>

/* MPEG-2 CRC32. section incl. own CRC -> 0 */
uint32_t crc32_mpeg(const unsigned char *data, size_t len);

#endif
