/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_TEST_TLS_FIXTURE_H
#define DVBIPITOOLS_TEST_TLS_FIXTURE_H

#include <check.h>
#include <stdio.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

static inline void tls_fixture_write_cert(const char *cert_path, const char *key_path) {
  EVP_PKEY_CTX *kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
  EVP_PKEY *pkey = NULL;
  X509 *x = X509_new();
  X509_NAME *name;
  X509V3_CTX v3;
  X509_EXTENSION *ext;
  FILE *f;

  ck_assert_ptr_nonnull(kctx);
  ck_assert_ptr_nonnull(x);
  ck_assert_int_eq(EVP_PKEY_keygen_init(kctx), 1);
  ck_assert_int_eq(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(kctx, NID_X9_62_prime256v1), 1);
  ck_assert_int_eq(EVP_PKEY_keygen(kctx, &pkey), 1);
  EVP_PKEY_CTX_free(kctx);

  ck_assert_int_eq(X509_set_version(x, 2), 1);
  ck_assert_int_eq(ASN1_INTEGER_set(X509_get_serialNumber(x), 1), 1);
  ck_assert_ptr_nonnull(X509_gmtime_adj(X509_getm_notBefore(x), -60));
  ck_assert_ptr_nonnull(X509_gmtime_adj(X509_getm_notAfter(x), 86400));
  ck_assert_int_eq(X509_set_pubkey(x, pkey), 1);
  name = X509_get_subject_name(x);
  ck_assert_int_eq(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char *)"localhost", -1, -1, 0), 1);
  ck_assert_int_eq(X509_set_issuer_name(x, name), 1);
  X509V3_set_ctx_nodb(&v3);
  X509V3_set_ctx(&v3, x, x, NULL, NULL, 0);
  ext = X509V3_EXT_conf_nid(NULL, &v3, NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1");
  ck_assert_ptr_nonnull(ext);
  ck_assert_int_eq(X509_add_ext(x, ext, -1), 1);
  X509_EXTENSION_free(ext);
  ck_assert_int_gt(X509_sign(x, pkey, EVP_sha256()), 0);

  f = fopen(key_path, "wb");
  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(PEM_write_PrivateKey(f, pkey, NULL, NULL, 0, NULL, NULL), 1);
  fclose(f);
  f = fopen(cert_path, "wb");
  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(PEM_write_X509(f, x), 1);
  fclose(f);
  X509_free(x);
  EVP_PKEY_free(pkey);
}

#endif
