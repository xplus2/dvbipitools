/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_SHA1_H
#define LIB_SHA1_H

#include <stddef.h>
#include <stdint.h>

/* RFC 3174. out: 20 raw bytes, not hex/base64 */
void sha1(const void *data, size_t len, uint8_t out[20]);

#endif
