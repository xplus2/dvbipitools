/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <grp.h>
#include <netinet/in.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pcap.h>

#include "lib/demux/rtp.h"
#include "lib/signal.h"

#include "capture.h"

struct capture {
  pcap_t *pcap;
  int dlt;
  const char *const *ranges; /* borrowed from the capture_open caller, must outlive this capture_t */
  size_t range_count;
};

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

static int cidr_parse(const char *s, cidr_t *c) {
  char buf[64];
  char *slash;
  int prefix;
  strncpy(buf, s, sizeof buf - 1);
  buf[sizeof buf - 1] = '\0';
  slash = strchr(buf, '/');
  if (!slash)
    return -1;
  *slash = '\0';
  prefix = atoi(slash + 1);

  if (strchr(buf, ':')) {
    if (prefix < 0 || prefix > 128 || inet_pton(AF_INET6, buf, &c->u.v6.addr) != 1)
      return -1;
    c->family = AF_INET6;
    c->u.v6.prefix = (unsigned)prefix;
    return 0;
  }
  if (prefix < 0 || prefix > 32 || inet_pton(AF_INET, buf, &c->u.v4.addr) != 1)
    return -1;
  c->family = AF_INET;
  c->u.v4.mask.s_addr = prefix ? htonl(0xFFFFFFFFu << (32 - prefix)) : 0;
  return 0;
}

static int v6_prefix_match(const struct in6_addr *a, const struct in6_addr *b, unsigned prefix) {
  unsigned full_bytes = prefix / 8, rem_bits = prefix % 8;
  if (full_bytes && memcmp(a->s6_addr, b->s6_addr, full_bytes) != 0)
    return 0;
  if (rem_bits) {
    unsigned char mask = (unsigned char)(0xFF << (8 - rem_bits));
    if ((a->s6_addr[full_bytes] & mask) != (b->s6_addr[full_bytes] & mask))
      return 0;
  }
  return 1;
}

static int in_ranges(int family, const void *addr, const char *const *ranges, size_t range_count) {
  size_t i;
  cidr_t c;
  for (i = 0; i < range_count; i++) {
    if (cidr_parse(ranges[i], &c) != 0 || c.family != family)
      continue;
    if (family == AF_INET) {
      const struct in_addr *a4 = (const struct in_addr *)addr;
      if ((a4->s_addr & c.u.v4.mask.s_addr) == (c.u.v4.addr.s_addr & c.u.v4.mask.s_addr))
        return 1;
    } else if (v6_prefix_match((const struct in6_addr *)addr, &c.u.v6.addr, c.u.v6.prefix)) {
      return 1;
    }
  }
  return 0;
}

static char *build_bpf(const char *const *ranges, size_t count) {
  size_t i, cap = 64, len = 0;
  char *out = malloc(cap);
  if (!out)
    return NULL;
  out[0] = '\0';
  for (i = 0; i < count; i++) {
    const char *proto = strchr(ranges[i], ':') ? "ip6" : "ip";
    size_t need = len + strlen(ranges[i]) + 32;
    int n;
    if (need > cap) {
      char *grown;
      cap = need * 2;
      grown = realloc(out, cap);
      if (!grown) {
        free(out);
        return NULL;
      }
      out = grown;
    }
    n = snprintf(out + len, cap - len, "%s(%s and dst net %s)", i ? " or " : "", proto, ranges[i]);
    len += (size_t)n;
  }
  return out;
}

capture_t *capture_open(const char *iface, const char *bpf_expr, const char *const *ranges, size_t range_count, char *errbuf, size_t errbuf_len) {
  capture_t *cap = calloc(1, sizeof *cap);
  char pcap_err[PCAP_ERRBUF_SIZE];
  char *auto_bpf = NULL;
  const char *filter;
  struct bpf_program prog;
  int rc;
  if (!cap) {
    snprintf(errbuf, errbuf_len, "out of memory");
    return NULL;
  }

  cap->ranges = ranges;
  cap->range_count = range_count;
  cap->pcap = pcap_create(iface ? iface : "any", pcap_err);
  if (!cap->pcap) {
    snprintf(errbuf, errbuf_len, "%s", pcap_err);
    free(cap);
    return NULL;
  }
  pcap_set_snaplen(cap->pcap, 65535);
  pcap_set_promisc(cap->pcap, 1);
  pcap_set_timeout(cap->pcap, 100);

  rc = pcap_activate(cap->pcap);
  if (rc < 0) {
    if (rc == PCAP_ERROR_PERM_DENIED)
      snprintf(errbuf, errbuf_len, "capture needs CAP_NET_RAW (setcap cap_net_raw+ep on the binary, or run as root and use -u to drop privileges after opening)");
    else
      snprintf(errbuf, errbuf_len, "%s", pcap_geterr(cap->pcap));
    pcap_close(cap->pcap);
    free(cap);
    return NULL;
  }

  cap->dlt = pcap_datalink(cap->pcap);
  if (cap->dlt != DLT_EN10MB && cap->dlt != DLT_LINUX_SLL) {
    snprintf(errbuf, errbuf_len, "unsupported datalink type %d", cap->dlt);
    pcap_close(cap->pcap);
    free(cap);
    return NULL;
  }

  filter = bpf_expr;
  if (!filter) {
    auto_bpf = build_bpf(ranges, range_count);
    if (!auto_bpf) {
      snprintf(errbuf, errbuf_len, "out of memory building capture filter");
      pcap_close(cap->pcap);
      free(cap);
      return NULL;
    }
    filter = auto_bpf;
  }

  if (pcap_compile(cap->pcap, &prog, filter, 1, PCAP_NETMASK_UNKNOWN) < 0) {
    snprintf(errbuf, errbuf_len, "bpf compile: %s", pcap_geterr(cap->pcap));
    free(auto_bpf);
    pcap_close(cap->pcap);
    free(cap);
    return NULL;
  }
  if (pcap_setfilter(cap->pcap, &prog) < 0) {
    snprintf(errbuf, errbuf_len, "bpf install: %s", pcap_geterr(cap->pcap));
    pcap_freecode(&prog);
    free(auto_bpf);
    pcap_close(cap->pcap);
    free(cap);
    return NULL;
  }
  pcap_freecode(&prog);
  free(auto_bpf);
  return cap;
}

void capture_close(capture_t *cap) {
  if (!cap)
    return;
  pcap_close(cap->pcap);
  free(cap);
}

int capture_drop_privileges(const char *user) {
  struct passwd *pw;
  if (!user)
    return 0;
  pw = getpwnam(user);
  if (!pw)
    return -1;
  if (setgid(pw->pw_gid) < 0)
    return -1;
  if (initgroups(pw->pw_name, pw->pw_gid) < 0)
    return -1;
  if (setuid(pw->pw_uid) < 0)
    return -1;
  return 0;
}

void capture_handle_frame(int dlt, const unsigned char *pkt, size_t len, const char *const *ranges, size_t range_count, channel_table_t *t, mcsend_table_t *mt, unsigned ff_port) {
  size_t off, ip_off, udp_off, rtp_off, payload_off;
  unsigned ethertype, dport;
  int family;
  struct in_addr dst4;
  struct in6_addr dst6;
  const void *dst_bytes;
  char group[64];
  uint16_t seq;
  uint32_t timestamp, ssrc;
  channel_t *c;

  if (dlt == DLT_EN10MB) {
    if (len < 14)
      return;
    ethertype = ((unsigned)pkt[12] << 8) | pkt[13];
    off = 14;
    if (ethertype == 0x8100) { /* single VLAN tag */
      if (len < off + 4)
        return;
      ethertype = ((unsigned)pkt[off + 2] << 8) | pkt[off + 3];
      off += 4;
    }
  } else if (dlt == DLT_LINUX_SLL) {
    if (len < 16)
      return;
    ethertype = ((unsigned)pkt[14] << 8) | pkt[15];
    off = 16;
  } else {
    return;
  }

  if (ethertype == 0x0800) {
    unsigned ihl, proto;

    ip_off = off;
    if (len < ip_off + 20 || (pkt[ip_off] >> 4) != 4)
      return;
    ihl = (unsigned)(pkt[ip_off] & 0x0F) * 4;
    proto = pkt[ip_off + 9];
    if (proto != 17 || len < ip_off + ihl + 8) /* UDP only */
      return;
    memcpy(&dst4, pkt + ip_off + 16, 4);
    family = AF_INET;
    dst_bytes = &dst4;
    udp_off = ip_off + ihl;
  } else if (ethertype == 0x86DD) {
    unsigned next_header;
    size_t hdr_off;

    ip_off = off;
    if (len < ip_off + 40 || (pkt[ip_off] >> 4) != 6)
      return;
    next_header = pkt[ip_off + 6];
    memcpy(&dst6, pkt + ip_off + 24, 16);
    hdr_off = ip_off + 40;

    for (;;) {
      if (next_header == 17) /* UDP */
        break;
      if (next_header == 0 || next_header == 60 || next_header == 43) {
        /* Hop-by-Hop / Destination Options / Routing: next-header(1) + len-in-8-octet-units-minus-1(1) + data */
        unsigned ext_len;
        if (len < hdr_off + 2)
          return;
        ext_len = pkt[hdr_off + 1];
        next_header = pkt[hdr_off];
        hdr_off += ((size_t)ext_len + 1) * 8;
        continue;
      }
      if (next_header == 44) { /* Fragment header: fixed 8 bytes */
        if (len < hdr_off + 8)
          return;
        next_header = pkt[hdr_off];
        hdr_off += 8;
        continue;
      }
      return; /* AH/ESP or anything else unsupported - documented scope limit */
    }
    if (len < hdr_off + 8)
      return;
    family = AF_INET6;
    dst_bytes = &dst6;
    udp_off = hdr_off;
  } else {
    return; /* not IPv4 or IPv6 */
  }

  if (!in_ranges(family, dst_bytes, ranges, range_count)) /* userspace whitelist, authoritative regardless of the BPF filter */
    return;

  dport = ((unsigned)pkt[udp_off + 2] << 8) | pkt[udp_off + 3];
  rtp_off = udp_off + 8;
  if (rtp_off > len)
    return;

  {
    size_t payload_rel = rtp_payload_offset(pkt + rtp_off, len - rtp_off);
    if (payload_rel == 0) /* not RTP-wrapped TS */
      return;
    payload_off = rtp_off + payload_rel;
  }

  seq = (uint16_t)(((unsigned)pkt[rtp_off + 2] << 8) | pkt[rtp_off + 3]);
  timestamp = ((uint32_t)pkt[rtp_off + 4] << 24) | ((uint32_t)pkt[rtp_off + 5] << 16) | ((uint32_t)pkt[rtp_off + 6] << 8) | pkt[rtp_off + 7];
  ssrc = ((uint32_t)pkt[rtp_off + 8] << 24) | ((uint32_t)pkt[rtp_off + 9] << 16) | ((uint32_t)pkt[rtp_off + 10] << 8) | pkt[rtp_off + 11];

  if (!inet_ntop(family, dst_bytes, group, sizeof group))
    return;
  c = channel_lookup(t, family, group, dport);
  if (!c) /* max-channels cap, already logged by channel_lookup */
    return;
  if (mt)
    mcsend_ensure(mt, c, ff_port); /* cheap no-op if c already has a socket - safe to call every frame */
  channel_store(c, ssrc, seq, timestamp, pkt + payload_off, len - payload_off);
}

typedef struct {
  int dlt;
  const char *const *ranges;
  size_t range_count;
  channel_table_t *t;
  mcsend_table_t *mt;
  unsigned ff_port;
} cb_ctx_t;

static void pcap_cb(unsigned char *user, const struct pcap_pkthdr *hdr, const unsigned char *pkt) {
  cb_ctx_t *ctx = (cb_ctx_t *)user;
  capture_handle_frame(ctx->dlt, pkt, hdr->caplen, ctx->ranges, ctx->range_count, ctx->t, ctx->mt, ctx->ff_port);
}

void capture_run(capture_t *cap, channel_table_t *t, mcsend_table_t *mt, unsigned ff_port) {
  cb_ctx_t ctx;
  ctx.dlt = cap->dlt;
  ctx.t = t;
  ctx.ranges = cap->ranges;
  ctx.range_count = cap->range_count;
  ctx.mt = mt;
  ctx.ff_port = ff_port;
  while (!signal_stop_requested()) {
    int rc = pcap_dispatch(cap->pcap, -1, pcap_cb, (unsigned char *)&ctx);
    if (rc < 0)
      break;
  }
}
