/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_CAPTURE_H
#define DIPIFCCRET_CAPTURE_H

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>

#include <linux/filter.h>

typedef struct capture capture_t;

/* one captured RTP-carried-TS packet, whitelist-checked. addr: raw network-order dst bytes,
   addr_len 4 (v4) or 16 (v6), not text, format with inet_ntop if needed.
   dscp: IPv4 TOS / IPv6 Traffic Class byte, top 6 bits (DSCP), ECN bits masked off */
typedef void (*capture_frame_cb)(int family, const void *addr, size_t addr_len, unsigned port, unsigned char dscp, uint32_t ssrc, uint16_t seq, uint32_t timestamp, const unsigned char *payload, size_t payload_len, void *user);

typedef struct {
  int family; /* AF_INET or AF_INET6 */
  union {
    struct {
      struct in_addr addr, mask;
    } v4;
    struct {
      struct in6_addr addr;
      unsigned prefix;
    } v6;
  } u;
} cidr_t;

/* parses "addr/prefix" (IPv4 or IPv6); 0 ok, -1 invalid syntax; exposed for testing */
int cidr_parse(const char *s, cidr_t *c);

/* addr: raw network-order bytes, family AF_INET or AF_INET6. 1 if within any range, else 0 */
int in_ranges(int family, const void *addr, const cidr_t *ranges, size_t range_count);

/* iface required, single Ethernet interface; a kernel-side filter matching ethertype/vlan/ranges is auto-built and attached; ranges (IPv4 or IPv6) also enforced in userspace regardless */
capture_t *capture_open(const char *iface, const char *const *ranges, size_t range_count, char *errbuf, size_t errbuf_len);

void capture_close(capture_t *cap);

/* setuid/setgid after capture_open; 0 on success */
int capture_drop_privileges(const char *user);

/* blocks until signal_stop_requested(), feeds captured RTP to cb */
void capture_run(capture_t *cap, capture_frame_cb cb, void *user);

/* parses one captured Ethernet frame (single VLAN tag unwrapped if present), invokes cb if in-range RTP; ranges must already be parsed (see cidr_parse); exposed for testing */
void capture_handle_frame(const unsigned char *pkt, size_t len, const cidr_t *ranges, size_t range_count, capture_frame_cb cb, void *user);

/* builds classic-BPF program matching ethertype+optional single vlan+dst-net ranges; caller frees; NULL on error (too many ranges, or out of memory); exposed for testing */
struct sock_filter *capture_build_bpf(const cidr_t *ranges, size_t range_count, size_t *out_len);

#endif
