/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_URIPARSE_H
#define LIB_URIPARSE_H

#include <stddef.h>

/* rest: addr:port or [addr6]:port, multicast required (224.0.0.0/4, ff00::/8). caller strips leading '@' first. */
int uriparse_mcast_addrport(const char *rest, int *family, char *group, size_t groupsz, unsigned *port);

/* rest: host[:port]/(rtp|udp)/path..., udpxy-style tspush. port_out defaults 80 if absent. */
int uriparse_udpxy(const char *rest, char *host_out, size_t host_cap, unsigned *port_out, int *rtp_wrapped_out, char *path_out, size_t path_cap);

/* classifies uri as rtmp/rtmps/file, copies into matching buffer.
   returns 0 = file, 1 = rtmp, 2 = rtmps, -1 if uri doesn't fit target buffer. */
int uriparse_rtmp_or_file(const char *uri, char *rtmp_buf, size_t rtmp_cap, char *file_buf, size_t file_cap);

#endif
