/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_BASE64_H
#define LIB_BASE64_H

#include <stddef.h>

/* RFC 4648, padded. out must be >= base64_encoded_len(len)+1 (NUL'd) */
size_t base64_encoded_len(size_t len);
void base64_encode(const void *data, size_t len, char *out);

#endif
