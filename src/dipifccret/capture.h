/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_CAPTURE_H
#define DIPIFCCRET_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

typedef struct capture capture_t;

/* one captured RTP-carried-TS packet, whitelist-checked; group is printable IP (v4 or v6) */
typedef void (*capture_frame_cb)(int family, const char *group, unsigned port, uint32_t ssrc, uint16_t seq, uint32_t timestamp, const unsigned char *payload, size_t payload_len, void *user);

/* iface NULL = libpcap "any"; bpf_expr overrides filter auto-built from ranges if non-NULL; ranges (IPv4 or IPv6) enforced in userspace either way */
capture_t *capture_open(const char *iface, const char *bpf_expr, const char *const *ranges, size_t range_count, char *errbuf, size_t errbuf_len);

void capture_close(capture_t *cap);

/* setuid/setgid after capture_open; 0 on success */
int capture_drop_privileges(const char *user);

/* blocks until signal_stop_requested(), feeds captured RTP to cb */
void capture_run(capture_t *cap, capture_frame_cb cb, void *user);

/* parses one captured frame, invokes cb if in-range RTP; exposed for testing, dlt is libpcap DLT_* value */
void capture_handle_frame(int dlt, const unsigned char *pkt, size_t len, const char *const *ranges, size_t range_count, capture_frame_cb cb, void *user);

#endif
