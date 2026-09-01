/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>

#include "lib/helper/ioutil.h"

#include "tlscert.h"

static int file_exists(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f)
    return 0;
  fclose(f);
  return 1;
}

int tlscert_find(const char *explicit_cert, const char *explicit_key, const char **cert_out, const char **key_out) {
  static const char *const dirs[] = {".", "/etc/dvbipitools", "/etc/dvbipitools/dipixy"};
  static char cert_buf[512], key_buf[512];

  if (explicit_cert && explicit_key) {
    *cert_out = explicit_cert;
    *key_out = explicit_key;
    return 1;
  }

  for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
    size_t dlen = bufcpy(cert_buf, sizeof cert_buf, dirs[i]);
    bufcpy(cert_buf + dlen, sizeof cert_buf - dlen, "/server.crt");
    dlen = bufcpy(key_buf, sizeof key_buf, dirs[i]);
    bufcpy(key_buf + dlen, sizeof key_buf - dlen, "/server.key");
    if (file_exists(cert_buf) && file_exists(key_buf)) {
      *cert_out = cert_buf;
      *key_out = key_buf;
      return 1;
    }
  }
  return 0;
}
