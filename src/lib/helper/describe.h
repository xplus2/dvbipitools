/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HELPER_DESCRIBE_H
#define DVBIPITOOLS_LIB_HELPER_DESCRIBE_H

#include <stddef.h>

void describe_mcast_uri(char *buf, size_t n, const char *scheme, int family, const char *group, unsigned port);
void describe_http_uri(char *buf, size_t n, int tls, const char *host, unsigned port, const char *path);
void describe_srt_uri(char *buf, size_t n, int family, int listen, const char *host, unsigned port);

#endif
