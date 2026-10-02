/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/base64.h"
#include "lib/sys/ioutil.h"

#include "priv.h"

#define ARGS_AUTH_CREDS_MAX 128 /* max "user:password" length for --auth */

int dixy_cfg_auth(const char *val, char *out, size_t outsz, char *err, size_t errsz) {
  const char *colon = strchr(val, ':');
  char b64[192];
  size_t n;
  if (!colon || colon == val) {
    snprintf(err, errsz, "invalid '%s' (need user:password)", val);
    return -1;
  }
  if (strlen(val) >= ARGS_AUTH_CREDS_MAX) {
    bufcpy(err, errsz, "credentials too long");
    return -1;
  }
  base64_encode(val, strlen(val), b64);
  n = bufcpy(out, outsz, "Basic ");
  bufcpy(out + n, outsz - n, b64);
  return 0;
}

int dixy_basic_auth_parse(const char *flag, const char *val, char *out, size_t outsz) {
  const char *colon = strchr(val, ':');
  char b64[192];
  size_t n;
  if (!colon || colon == val) {
    argerr("invalid %s: %s (need user:password)", flag, val);
    return -1;
  }
  if (strlen(val) >= ARGS_AUTH_CREDS_MAX) {
    argerr("%s credentials too long: %s", flag, val);
    return -1;
  }
  base64_encode(val, strlen(val), b64);
  n = bufcpy(out, outsz, "Basic ");
  bufcpy(out + n, outsz - n, b64);
  return 0;
}

