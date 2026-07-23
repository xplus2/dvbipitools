/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRET_CAPTURE_H
#define DIPIRET_CAPTURE_H

#include <stddef.h>

#include "channel.h"
#include "mcsend.h"

typedef struct capture capture_t;

/* iface NULL = libpcap "any"; bpf_expr overrides the filter auto-built from ranges if non-NULL; ranges (IPv4 or IPv6) enforced in userspace either way */
capture_t *capture_open(const char *iface, const char *bpf_expr, const char *const *ranges, size_t range_count, char *errbuf, size_t errbuf_len);

void capture_close(capture_t *cap);

int capture_drop_privileges(const char *user); /* setuid/setgid after capture_open; 0 on success */

/* blocks until signal_stop_requested(), feeds captured RTP into t; mt NULL = --no-mc-ret, skip mcsend_ensure entirely */
void capture_run(capture_t *cap, channel_table_t *t, mcsend_table_t *mt, unsigned ff_port);

/* parses one captured frame and stores it if in-range RTP; exposed for testing, dlt is a libpcap DLT_* value; mt NULL = --no-mc-ret */
void capture_handle_frame(int dlt, const unsigned char *pkt, size_t len, const char *const *ranges, size_t range_count, channel_table_t *t, mcsend_table_t *mt, unsigned ff_port);

#endif
