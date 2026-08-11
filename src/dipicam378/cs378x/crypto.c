/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <openssl/evp.h>

#include "cs378x.h"

static uint32_t crc32_table[256];

void cs378x_crc32_init_table(void) {
  uint32_t c;
  int n, k;
  for (n = 0; n < 256; n++) {
    c = (uint32_t)n;
    for (k = 0; k < 8; k++)
      c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    crc32_table[n] = c;
  }
}

/* zlib-standard CRC-32 (reflected, poly 0xEDB88320) */
uint32_t cs378x_crc32(const unsigned char *buf, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  size_t i;
  for (i = 0; i < len; i++)
    crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

int cs378x_md5(const unsigned char *data, size_t len, unsigned char out[16]) {
  unsigned int outlen = 0;
  return (EVP_Digest(data, len, out, &outlen, EVP_md5(), NULL) == 1 && outlen == 16) ? 0 : -1;
}

/* AES-128, plain ECB, no padding */
int cs378x_aes128_ecb(const unsigned char key[16], unsigned char *buf, size_t len, int encrypt) {
  EVP_CIPHER_CTX *ctx;
  int outlen, ok = 0;
  if (len == 0 || len % 16 != 0)
    return -1;
  ctx = EVP_CIPHER_CTX_new();
  if (!ctx)
    return -1;
  if (encrypt ? EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL) != 1
              : EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL) != 1)
    goto done;
  EVP_CIPHER_CTX_set_padding(ctx, 0);
  if (encrypt ? EVP_EncryptUpdate(ctx, buf, &outlen, buf, (int)len) != 1 : EVP_DecryptUpdate(ctx, buf, &outlen, buf, (int)len) != 1)
    goto done;
  ok = ((size_t)outlen == len);
done:
  EVP_CIPHER_CTX_free(ctx);
  return ok ? 0 : -1;
}

/* rounds n up to the next multiple of 16 */
size_t cs378x_frame_boundary(size_t n) {
  return (((n - 1) >> 4) + 1) << 4;
}
