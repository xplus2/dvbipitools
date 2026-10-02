/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_TESTS_UNIT_DIPIDESCRAMBLE_CAS_FIXTURE_H
#define DVBIPITOOLS_TESTS_UNIT_DIPIDESCRAMBLE_CAS_FIXTURE_H

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include "dipidescramble/crypto.h"
#include "dipidescramble/device.h"

#define SC_SECTION_TID_EMM 0x82
#define SC_SECTION_TID_ECM_EVEN 0x80
#define TEST_SERIAL "test-serial-01"
#define TEST_ECM_PID 0x0020

static char g_key_path[] = "/tmp/dipidescramble_test_device_key_XXXXXX";
static const ecm_profile_t no_profile; /* zero-initialized: profile.set == 0, legacy path */

static inline void section_header(unsigned char table_id, size_t payload_len, unsigned char out[3]) {
  out[0] = table_id;
  out[1] = (unsigned char)(0x70 | ((payload_len >> 8) & 0x0F));
  out[2] = (unsigned char)(payload_len & 0xFF);
}

static inline EVP_PKEY *make_rsa_key(void) {
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
  EVP_PKEY *pkey = NULL;

  ck_assert_int_gt(EVP_PKEY_keygen_init(ctx), 0);
  ck_assert_int_gt(EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048), 0);
  ck_assert_int_gt(EVP_PKEY_keygen(ctx, &pkey), 0);
  EVP_PKEY_CTX_free(ctx);
  return pkey;
}

static inline void write_key_pem(const EVP_PKEY *pkey, char *path_template) {
  int fd = mkstemp(path_template);
  FILE *f;

  ck_assert_int_ge(fd, 0);
  f = fdopen(fd, "w");
  ck_assert_int_eq(PEM_write_PrivateKey(f, pkey, NULL, NULL, 0, NULL, NULL), 1);
  fclose(f);
}

static inline device_state_t *make_device_for_key(EVP_PKEY *pkey, const char *serial, size_t max_services) {
  device_state_t *d;

  strcpy(g_key_path, "/tmp/dipidescramble_test_device_key_XXXXXX");
  write_key_pem(pkey, g_key_path);

  d = device_state_new(g_key_path, serial, &no_profile, max_services);
  ck_assert_ptr_nonnull(d);
  remove(g_key_path);
  return d;
}

static inline device_state_t *make_device_serial(EVP_PKEY **pub_out, const char *serial, size_t max_services) {
  EVP_PKEY *pkey = make_rsa_key();
  device_state_t *d = make_device_for_key(pkey, serial, max_services);

  *pub_out = pkey;
  return d;
}

static inline device_state_t *make_device(EVP_PKEY **pub_out) {
  return make_device_serial(pub_out, TEST_SERIAL, 0);
}

/* builds a real EMM-U section: header + [1B addr_len][serial][2B bk_version][RSA-OAEP(bk)] */
static inline size_t build_emm_u_for(EVP_PKEY *pub, const unsigned char bk[CRYPTO_KEY_LEN], const char *serial, unsigned char *out, size_t cap) {
  size_t addr_len = strlen(serial);
  unsigned char ct[512];
  size_t ctlen = sizeof ct;
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pub, NULL);
  size_t payload_len;

  ck_assert_int_gt(EVP_PKEY_encrypt_init(ctx), 0);
  ck_assert_int_gt(EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING), 0);
  ck_assert_int_gt(EVP_PKEY_encrypt(ctx, ct, &ctlen, bk, CRYPTO_KEY_LEN), 0);
  EVP_PKEY_CTX_free(ctx);

  payload_len = 1 + addr_len + 2 + ctlen;
  ck_assert_uint_le(3 + payload_len, cap);

  section_header(SC_SECTION_TID_EMM, payload_len, out);
  out[3] = (unsigned char)addr_len;
  memcpy(out + 4, serial, addr_len);
  out[4 + addr_len] = 0;
  out[5 + addr_len] = 1; /* bk_version, arbitrary */
  memcpy(out + 6 + addr_len, ct, ctlen);
  return 3 + payload_len;
}

static inline size_t build_emm_u(EVP_PKEY *pub, const unsigned char bk[CRYPTO_KEY_LEN], unsigned char *out, size_t cap) {
  return build_emm_u_for(pub, bk, TEST_SERIAL, out, cap);
}

/* builds a real EMM-G section: header + [2B service_id][2B sk_version][AES-256-GCM(sk under bk)] */
static inline size_t build_emm_g(const unsigned char bk[CRYPTO_KEY_LEN], const unsigned char sk[CRYPTO_KEY_LEN], unsigned service_id, unsigned char *out, size_t cap) {
  unsigned char *blob;
  unsigned char *nonce;
  unsigned char *ct;
  unsigned char *tag;
  EVP_CIPHER_CTX *ctx;
  int len = 0;
  size_t payload_len = 4 + CRYPTO_EMM_G_LEN;

  ck_assert_uint_le(3 + payload_len, cap);
  section_header(SC_SECTION_TID_EMM, payload_len, out);
  out[3] = (unsigned char)(service_id >> 8);
  out[4] = (unsigned char)service_id;
  out[5] = 0;
  out[6] = 1; /* sk_version, arbitrary */

  blob = out + 7;
  nonce = blob;
  ct = blob + CRYPTO_GCM_NONCE_LEN;
  tag = blob + CRYPTO_GCM_NONCE_LEN + CRYPTO_KEY_LEN;
  memset(nonce, 0x24, CRYPTO_GCM_NONCE_LEN);

  ctx = EVP_CIPHER_CTX_new();
  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, CRYPTO_GCM_NONCE_LEN, NULL), 1);
  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, NULL, NULL, bk, nonce), 1);
  ck_assert_int_eq(EVP_EncryptUpdate(ctx, ct, &len, sk, CRYPTO_KEY_LEN), 1);
  ck_assert_int_eq(EVP_EncryptFinal_ex(ctx, ct + len, &len), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, CRYPTO_GCM_TAG_LEN, tag), 1);
  EVP_CIPHER_CTX_free(ctx);

  return 3 + payload_len;
}

/* builds a real ECM section: header + CP_CW_COMBINATION's cp_number(2) +
   AES-256-ECB(cw zero-padded to 16 bytes, under sk) - matches the real
   Simulcrypt ECMG's ECM payload layout */
static inline size_t build_ecm(const unsigned char sk[CRYPTO_KEY_LEN], const unsigned char *cw, int cw_len, unsigned char *out, size_t cap) {
  unsigned char block[CRYPTO_CW_ENC_LEN];
  EVP_CIPHER_CTX *ctx;
  int len = 0;

  ck_assert_uint_le(5 + CRYPTO_CW_ENC_LEN, cap);
  memset(block, 0, sizeof block);
  memcpy(block, cw, (size_t)cw_len);

  section_header(SC_SECTION_TID_ECM_EVEN, 2 + CRYPTO_CW_ENC_LEN, out);
  out[3] = 0;
  out[4] = 1; /* cp_number, arbitrary */
  ctx = EVP_CIPHER_CTX_new();
  ck_assert_int_eq(EVP_EncryptInit_ex(ctx, EVP_aes_256_ecb(), NULL, sk, NULL), 1);
  ck_assert_int_eq(EVP_CIPHER_CTX_set_padding(ctx, 0), 1);
  ck_assert_int_eq(EVP_EncryptUpdate(ctx, out + 5, &len, block, CRYPTO_CW_ENC_LEN), 1);
  EVP_CIPHER_CTX_free(ctx);

  return 5 + CRYPTO_CW_ENC_LEN;
}

typedef struct {
  int listen_fd;
  const char *response;
  size_t response_len;
} server_arg_t;

static inline void *serve_once(void *arg) {
  const server_arg_t *a = arg;
  int cfd = accept(a->listen_fd, NULL, NULL);
  struct timeval tv = {2, 0};
  char buf[4096];
  size_t got = 0;

  if (cfd < 0) return NULL;
  setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  for (;;) {
    ssize_t n = recv(cfd, buf + got, sizeof buf - got, 0);
    if (n <= 0)
      break;
    got += (size_t)n;
    if (got >= 4 && memcmp(buf + got - 4, "\r\n\r\n", 4) == 0)
      break;
  }
  send(cfd, a->response, a->response_len, 0);
  close(cfd);
  return NULL;
}

static inline int make_listener(unsigned *port_out) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;

  if (fd < 0) return fd;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(fd, 1), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

/* independent RFC 5869 HKDF-SHA256 (zero salt, single-block expand), not calling crypto.c's own
   crypto_hkdf_sha256 - this is the test's own reference implementation */
static inline void ref_hkdf(const unsigned char key[CRYPTO_KEY_LEN], const char *info, unsigned char out[32]) {
  unsigned char zero_salt[32] = {0};
  unsigned char prk[32];
  unsigned char t1[64];
  size_t infolen = strlen(info);
  unsigned int outlen;

  ck_assert_ptr_nonnull(HMAC(EVP_sha256(), zero_salt, sizeof zero_salt, key, CRYPTO_KEY_LEN, prk, &outlen));
  memcpy(t1, info, infolen);
  t1[infolen] = 0x01;
  ck_assert_ptr_nonnull(HMAC(EVP_sha256(), prk, sizeof prk, t1, infolen + 1, out, &outlen));
}

#endif
