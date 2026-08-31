/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_DLNA_SSDP_H
#define DIPIXY_DLNA_SSDP_H

#include <stddef.h>

#include "../args.h"

/* stable, deterministic from cfg->dlna_host. out[37]: 36 hex/dash chars + NUL, no "uuid:" prefix */
void ssdp_device_uuid(const config_t *cfg, char out[37]);

/* M-SEARCH header field lookup. headers starts past request line. 1 found, 0 not */
int ssdp_msearch_header(const char *headers, const char *name, char *out, size_t outsz);

/* spawns background thread: periodic ssdp:alive NOTIFY, answers M-SEARCH, ssdp:byebye on ssdp_stop().
   noop if cfg->enable_dlna false. IPv4 only, no IPv6 SSDP */
void ssdp_start(const config_t *cfg);

/* sends ssdp:byebye, stops/joins thread. safe if never started */
void ssdp_stop(void);

#endif
