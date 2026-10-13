/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_DLNA_SSDP_H
#define DIPIXY_DLNA_SSDP_H

#include <stddef.h>
#include <sys/socket.h>

#include "../cli/args.h"

/* stable, deterministic from cfg->dlna_host. out[37]: 36 hex/dash chars + NUL, no "uuid:" prefix */
void ssdp_device_uuid(const config_t *cfg, char out[37]);

/* M-SEARCH header field lookup. headers starts past request line. 1 found, 0 not */
int ssdp_msearch_header(const char *headers, const char *name, char *out, size_t outsz);

typedef struct {
  unsigned mask; /* bit 0 device uuid, bit 1+i i-th type */
  unsigned mx_s; /* reply delay bound, clamped to 5 */
} ssdp_query_t;

/* 1 if buf = valid M-SEARCH (MAN "ssdp:discover", MX 1..120) for a target we serve. 0 otherwise */
int ssdp_msearch_parse(const char *buf, const char *uuid, ssdp_query_t *q);

/* unicast answer per bit set in mask, sent from fd */
void ssdp_msearch_reply(int fd, const struct sockaddr *peer, socklen_t peerlen, const config_t *cfg, const char *uuid, unsigned mask);

/* spawn bg thread: periodic ssdp:alive NOTIFY, answers mcast M-SEARCH after random MX delay, ssdp:byebye on ssdp_stop().
   noop if cfg->enable_dlna false. IPv4 only, no IPv6 SSDP */
void ssdp_start(const config_t *cfg);

/* sends ssdp:byebye, stops/joins thread. safe if never started */
void ssdp_stop(void);

#endif
