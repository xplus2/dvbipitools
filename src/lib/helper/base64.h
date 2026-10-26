/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HELPER_BASE64_H
#define DVBIPITOOLS_LIB_HELPER_BASE64_H

#include <stddef.h>

/* RFC 4648, padded. out must be >= base64_encoded_len(len)+1 (NUL'd) */
size_t base64_encoded_len(size_t len);
void base64_encode(const void *data, size_t len, char *out);
/* RFC 4648, whitespace skipped. 0 ok, -1 invalid or cap exceeded */
int base64_decode(const char *s, unsigned char *out, size_t cap, size_t *len);

#endif
