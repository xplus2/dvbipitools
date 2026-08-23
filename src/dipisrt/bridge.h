/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISRT_BRIDGE_H
#define DIPISRT_BRIDGE_H

#include <srt/srt.h>
#include <sys/socket.h>

#include "lib/metrics/export.h"
#include "lib/net/srtout.h"
#include "lib/net/tssink.h"
#include "lib/net/tssource.h"

#include "args.h"

/* exposed for unit testing, not CLI-facing */
socklen_t addr_len(int family);
void build_addr(int family, const char *host, unsigned port, struct sockaddr_storage *ss);
srtout_group_mode_t group_mode_of(srt_group_sel_t g);
void nonsrt_to_tssrc_cfg(const nonsrt_t *s, const char *iface, int insecure_tls, tssrc_cfg_t *tc);
void nonsrt_to_tssink_cfg(const nonsrt_t *s, const char *iface, tssink_cfg_t *tk);

/* runs until stop sig or error. 0 clean stop, 1 error */
int bridge_run(const config_t *cfg, metrics_exporter_t *mx);

#endif
