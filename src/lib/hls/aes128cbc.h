/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HLS_AES128CBC_H
#define DVBIPITOOLS_LIB_HLS_AES128CBC_H

#include <stddef.h>

/* in-place. len nonzero multiple of 16. 0 ok, -1 bad args. */
int aes128cbc_decrypt(const unsigned char key[16], const unsigned char iv[16], unsigned char *data, size_t len);

/* Decrypt + strip PKCS7, *len updated. 0 ok, -1 bad args/padding. */
int aes128cbc_decrypt_pkcs7(const unsigned char key[16], const unsigned char iv[16], unsigned char *data, size_t *len);

#endif
