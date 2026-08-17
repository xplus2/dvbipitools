/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include "auth.h"

static int md5_b64(const char *s, char *out, size_t cap) {
  unsigned char digest[EVP_MAX_MD_SIZE], b64[32];
  unsigned int dlen = 0;
  size_t n;

  if (!EVP_Digest(s, strlen(s), digest, &dlen, EVP_md5(), NULL) || dlen != 16)
    return -1;
  EVP_EncodeBlock(b64, digest, (int)dlen);
  n = strlen((char *)b64);
  if (n >= cap)
    return -1;
  memcpy(out, b64, n + 1);
  return 0;
}

int rtmp_auth_adobe_response(const char *user, const char *password, const char *salt, const char *opaque, const char *server_challenge, char *challenge2_out, size_t challenge2_cap, char *response_out, size_t response_cap) {
  char hash1[32], buf[512];
  unsigned char rnd[8];

  if (!opaque && !server_challenge)
    return -1;

  if ((size_t)snprintf(buf, sizeof buf, "%s%s%s", user, salt, password) >= sizeof buf)
    return -1;
  if (md5_b64(buf, hash1, sizeof hash1))
    return -1;

  if (1 != RAND_bytes(rnd, sizeof rnd))
    return -1;
  if ((size_t)snprintf(challenge2_out, challenge2_cap, "%02x%02x%02x%02x%02x%02x%02x%02x", rnd[0], rnd[1], rnd[2], rnd[3], rnd[4], rnd[5], rnd[6], rnd[7]) >= challenge2_cap)
    return -1;

  if ((size_t)snprintf(buf, sizeof buf, "%s%s%s", hash1, opaque ? opaque : server_challenge, challenge2_out) >= sizeof buf)
    return -1;
  return md5_b64(buf, response_out, response_cap);
}
