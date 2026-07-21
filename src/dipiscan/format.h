/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISCAN_FORMAT_H
#define DIPISCAN_FORMAT_H

#include <stdio.h>

#include "args.h"

/* playlist header. invocation: argv[0]-ish string for m3u comment */
void format_init(FILE *f, out_fmt_t fmt, const char *invocation);

/* station entry. tsid/onid/sid are the DVB triplet (transport_stream_id, original_network_id, service_id) */
void format_item(FILE *f, out_fmt_t fmt, const char *name, const char *uri, unsigned tsid, unsigned onid, unsigned sid);

/* playlist footer */
void format_close(FILE *f, out_fmt_t fmt);

#endif
