/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "reactor_tls_int.h"

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <stdio.h>
#include <string.h>

#include "lib/helper/ioutil.h"

static void asn1time_to_str(const ASN1_TIME *t, char *buf, size_t sz) {
  BIO *bio = BIO_new(BIO_s_mem());
  if (!bio) {
    bufcpy(buf, sz, "(unknown)");
    return;
  }
  ASN1_TIME_print(bio, t);
  char tmp[64] = "";
  int n = BIO_read(bio, tmp, (int)(sizeof(tmp) - 1));
  if (n > 0)
    tmp[n] = '\0';
  BIO_free(bio);
  bufcpy(buf, sz, tmp);
}

static void x509_to_info(X509 *cert, char *buf, size_t sz) {
  char cn[256] = "(unknown)";
  X509_NAME *subj = X509_get_subject_name(cert);
  if (subj) X509_NAME_get_text_by_NID(subj, NID_commonName, cn, (int)sizeof(cn));
  char nb[64], na[64];
  asn1time_to_str(X509_get0_notBefore(cert), nb, sizeof(nb));
  asn1time_to_str(X509_get0_notAfter(cert), na, sizeof(na));

  {
    size_t off = bufcpy(buf, sz, "CN=");
    off += bufcpy(buf + off, sz - off, cn);
    off += bufcpy(buf + off, sz - off, "  valid ");
    off += bufcpy(buf + off, sz - off, nb);
    off += bufcpy(buf + off, sz - off, " - ");
    bufcpy(buf + off, sz - off, na);
  }
}

void tls_cert_info(char *buf, size_t sz, const char *path, int from_file) {
  if (!from_file) {
    if (!g_ssl_ctx) {
      bufcpy(buf, sz, "(TLS not initialised)");
      return;
    }
    X509 *cert = SSL_CTX_get0_certificate(g_ssl_ctx);
    if (!cert) {
      bufcpy(buf, sz, "(no certificate loaded)");
      return;
    }
    x509_to_info(cert, buf, sz);
  } else {
    if (!path || !path[0]) {
      bufcpy(buf, sz, "(no path configured)");
      return;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
      size_t off = bufcpy(buf, sz, "(cannot open ");
      off += bufcpy(buf + off, sz - off, path);
      bufcpy(buf + off, sz - off, ")");
      return;
    }
    X509 *cert = PEM_read_X509(f, NULL, NULL, NULL);
    fclose(f);
    if (!cert) {
      size_t off = bufcpy(buf, sz, "(failed to parse ");
      off += bufcpy(buf + off, sz - off, path);
      bufcpy(buf + off, sz - off, ")");
      return;
    }
    x509_to_info(cert, buf, sz);
    X509_free(cert);
  }
}

static void x509_to_detail(X509 *cert, tls_cert_detail_t *out) {
  memset(out, 0, sizeof(*out));

  X509_NAME *subj = X509_get_subject_name(cert);
  if (subj)
    X509_NAME_get_text_by_NID(subj, NID_commonName, out->cn,(int)sizeof(out->cn));
  else
    bufcpy(out->cn, sizeof(out->cn), "(unknown)");

  asn1time_to_str(X509_get0_notBefore(cert), out->valid_from,sizeof(out->valid_from));
  asn1time_to_str(X509_get0_notAfter(cert), out->valid_to,sizeof(out->valid_to));
  /* Subject Alternative Names (DNS entries) */
  GENERAL_NAMES *sans = (GENERAL_NAMES *)X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
  if (sans) {
    int n = sk_GENERAL_NAME_num(sans);
    for (int i = 0; i < n && out->alias_count < 16; i++) {
      GENERAL_NAME *gn = sk_GENERAL_NAME_value(sans, i);
      if (gn->type == GEN_DNS) {
        const char *dns = (const char *)ASN1_STRING_get0_data(gn->d.ia5);
        if (dns) {
          strncpy(out->aliases[out->alias_count], dns,sizeof(out->aliases[0]) - 1);
          out->alias_count++;
        }
      }
    }
    GENERAL_NAMES_free(sans);
  }
}

int tls_cert_detail(const char *path, int from_file, tls_cert_detail_t *out) {
  memset(out, 0, sizeof(*out));
  if (!from_file) {
    if (!g_ssl_ctx) return 0;
    X509 *cert = SSL_CTX_get0_certificate(g_ssl_ctx);
    if (!cert) return 0;
    x509_to_detail(cert, out);
    return 1;
  } else {
    if (!path || !path[0]) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    X509 *cert = PEM_read_X509(f, NULL, NULL, NULL);
    fclose(f);
    if (!cert) return 0;
    x509_to_detail(cert, out);
    X509_free(cert);
    return 1;
  }
}
