/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_CHANNELS_PRIV_H
#define DIPIXY_CHANNELS_PRIV_H

#include "channels.h"

/* channels.c */
void wait_readers_quiescent(void);

/* build.c */
void build_from_m3u(channel_list_t *l, const char *path, int insecure_tls);
void build_from_xspf(channel_list_t *l, const char *path, int insecure_tls);
void build_from_csv(channel_list_t *l, const char *path, int insecure_tls);
void build_from_xml(channel_list_t *l, const char *path);
void build_from_http(channel_list_t *l, const char *url, int insecure_tls);
void build_from_sds(channel_list_t *l, const char *addrport, const char *iface, double timeout_s);

#endif
