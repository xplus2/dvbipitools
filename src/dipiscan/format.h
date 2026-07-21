/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISCAN_FORMAT_H
#define DIPISCAN_FORMAT_H

#include <stdio.h>

#include "args.h"

/* playlist header. invocation: argv[0]-ish string for m3u comment. provider: DomainName, OUT_XML only */
void format_init(FILE *f, out_fmt_t fmt, const char *invocation, const char *provider);

/* station entry. tsid/onid/sid are the DVB triplet (transport_stream_id, original_network_id, service_id).
 * family/group/port/rtp are the same address parsed into uri, needed separately for OUT_XML */
void format_item(FILE *f, out_fmt_t fmt, const char *name, const char *uri, int family, const char *group, unsigned port, int rtp, unsigned tsid, unsigned onid, unsigned sid);

/* playlist footer */
void format_close(FILE *f, out_fmt_t fmt);

#endif
