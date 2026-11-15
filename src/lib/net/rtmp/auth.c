/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include "auth.h"
#include "lib/helper/strutil.h"

static int md5_b64(const char *s, char *out, size_t cap) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned char b64[32];
  unsigned int dlen = 0;
  size_t n;

  if (!EVP_Digest(s, strlen(s), digest, &dlen, EVP_md5(), NULL) || dlen != 16) return -1;
  EVP_EncodeBlock(b64, digest, (int)dlen);
  n = strlen((char *)b64);
  if (n >= cap) return -1;
  memcpy(out, b64, n + 1);
  return 0;
}

int rtmp_auth_adobe_response(const char *user, const char *password, const char *salt, const char *opaque, const char *server_challenge, char *challenge2_out, size_t challenge2_cap, char *response_out, size_t response_cap) {
  char hash1[32];
  char buf[512];
  unsigned char rnd[8];
  sbuf_t sb;

  if (!opaque && !server_challenge) return -1;
  sbuf_init(&sb, buf, sizeof buf);
  sbuf_add(&sb, user);
  sbuf_add(&sb, salt);
  sbuf_add(&sb, password);
  if (sb.truncated || md5_b64(buf, hash1, sizeof hash1)) return -1;
  if (1 != RAND_bytes(rnd, sizeof rnd)) return -1;
  sbuf_init(&sb, challenge2_out, challenge2_cap);
  for (size_t i = 0; i < sizeof rnd; i++) sbuf_add_hex2(&sb, rnd[i]);
  if (sb.truncated) return -1;
  sbuf_init(&sb, buf, sizeof buf);
  sbuf_add(&sb, hash1);
  sbuf_add(&sb, opaque ? opaque : server_challenge);
  sbuf_add(&sb, challenge2_out);
  if (sb.truncated) return -1;
  return md5_b64(buf, response_out, response_cap);
}
