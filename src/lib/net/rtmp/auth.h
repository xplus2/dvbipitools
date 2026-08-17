/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_RTMP_AUTH_H
#define DVBIPITOOLS_LIB_NET_RTMP_AUTH_H

#include <stddef.h>

/* authmod=adobe challenge/response, ffmpeg-compatible. opaque xor server_challenge: pass NULL for other. 0 ok, -1 fail. */
int rtmp_auth_adobe_response(const char *user, const char *password, const char *salt, const char *opaque, const char *server_challenge, char *challenge2_out, size_t challenge2_cap, char *response_out, size_t response_cap);

#endif
