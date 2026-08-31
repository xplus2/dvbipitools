/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_DLNA_GENA_H
#define DIPIXY_DLNA_GENA_H

#include <stddef.h>

#include "../args.h"

void gena_subscribe_new(const config_t *cfg, const char *service, const char *callback_hdr, char *out_sid, size_t out_sidsz);

void gena_renew(const char *sid_hdr, char *out_sid, size_t out_sidsz);

void gena_unsubscribe(const char *sid_hdr);

void gena_notify_system_update(void);

unsigned gena_system_update_id(void);

#endif
