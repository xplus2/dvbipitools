/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRIST_BRIDGE_H
#define DIPIRIST_BRIDGE_H

#include <librist/librist.h>

#include "lib/metrics/export.h"
#include "lib/net/tssink.h"
#include "lib/net/tssource.h"

#include "args.h"

/* exposed for unit testing, not CLI-facing */
enum rist_profile profile_of(rist_profile_sel_t p);
void nonrist_to_tssrc_cfg(const nonrist_t *s, const char *iface, int insecure_tls, tssrc_cfg_t *tc);
void nonrist_to_tssink_cfg(const nonrist_t *s, const char *iface, tssink_cfg_t *tk);

/* run until stop sig or error. 0 clean stop, 1 error */
int bridge_run(const config_t *cfg, metrics_exporter_t *mx);

#endif
