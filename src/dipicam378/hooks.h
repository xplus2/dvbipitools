/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPICAM378_HOOKS_H
#define DIPICAM378_HOOKS_H

#include <stddef.h>

#include "lib/metrics/export.h"

#include "cs378x/cs378x.h"
#include "device.h"

/* snapshot per exporter interval */
void cam378_push_metrics(metrics_exporter_t *mx, const cs378x_server_t *srv, device_state_t *dev, const char *algo_name);

int cam378_ecm_cb(const unsigned char *ecm, size_t ecm_len, unsigned srvid, unsigned caid, unsigned prid, unsigned char cw_out[16], void *user);
void cam378_emm_cb(const unsigned char *emm, size_t emm_len, unsigned caid, unsigned provid, void *user);

#endif
