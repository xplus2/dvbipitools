/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "auth.h"
#include "lib/helper/log.h"

int rtmp_auth_adobe_response(const char *user, const char *password, const char *salt, const char *opaque, const char *server_challenge, char *challenge2_out, size_t challenge2_cap, char *response_out, size_t response_cap) {
  (void)user;
  (void)password;
  (void)salt;
  (void)opaque;
  (void)server_challenge;
  (void)challenge2_out;
  (void)challenge2_cap;
  (void)response_out;
  (void)response_cap;
  log_line("rtmp: authmod=adobe needs OpenSSL, built without it, credentials ignored");
  return -1;
}
