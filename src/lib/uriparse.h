/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_URIPARSE_H
#define LIB_URIPARSE_H

#include <stddef.h>

/* rest: addr:port or [addr6]:port, multicast required (224.0.0.0/4, ff00::/8). caller strips leading '@' first. */
int uriparse_mcast_addrport(const char *rest, int *family, char *group, size_t groupsz, unsigned *port);

/* group:port, or [group]:port when family is AF_INET6 */
void uriparse_mcast_describe(int family, const char *group, unsigned port, char *buf, size_t n);

/* classifies uri as rtmp/rtmps/file, copies into matching buffer.
   returns 0 = file, 1 = rtmp, 2 = rtmps, -1 if uri doesn't fit target buffer. */
int uriparse_rtmp_or_file(const char *uri, char *rtmp_buf, size_t rtmp_cap, char *file_buf, size_t file_cap);

#endif
