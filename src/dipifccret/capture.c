/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <errno.h>
#include <grp.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <poll.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/filter.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include "lib/demux/rtp.h"
#include "lib/signal.h"

#include "capture.h"

#define CAPTURE_TP_BLOCK_SIZE (1u << 20) /* 1 MiB, page-multiple */
#define CAPTURE_TP_BLOCK_NR 64u          /* 64 MiB ring total */
#define CAPTURE_TP_FRAME_SIZE 2048u      /* covers 1500 MTU + 1 vlan tag + tpacket3_hdr */
#define CAPTURE_TP_RETIRE_TOV_MS 60u     /* block handed to userspace even under light traffic */
#define CAPTURE_POLL_TIMEOUT_MS 100
#define CAPTURE_BPF_MAX_INSNS 4096u /* kernel classic-BPF program length limit */

struct capture {
  int fd;
  unsigned char *ring;
  size_t ring_size;
  size_t block_size;
  size_t block_nr;
  size_t block_idx;
  cidr_t *parsed_ranges;
  size_t range_count;
};

int cidr_parse(const char *s, cidr_t *c) {
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

static int in_ranges(int family, const void *addr, const cidr_t *ranges, size_t range_count) {
  size_t i;
  for (i = 0; i < range_count; i++) {
    const cidr_t *c = &ranges[i];
    if (c->family != family)
      continue;
    if (family == AF_INET) {
      const struct in_addr *a4 = (const struct in_addr *)addr;
      if ((a4->s_addr & c->u.v4.mask.s_addr) == (c->u.v4.addr.s_addr & c->u.v4.mask.s_addr))
        return 1;
    } else if (v6_prefix_match((const struct in6_addr *)addr, &c->u.v6.addr, c->u.v6.prefix)) {
      return 1;
    }
  }
  return 0;
}

typedef struct {
  struct sock_filter *insns;
  size_t len, cap;
} bpf_buf_t;

static int bpf_emit(bpf_buf_t *b, struct sock_filter insn) {
  if (b->len == b->cap) {
    size_t new_cap = b->cap ? b->cap * 2 : 64;
    struct sock_filter *grown = realloc(b->insns, new_cap * sizeof *grown);
    if (!grown)
      return -1;
    b->insns = grown;
    b->cap = new_cap;
  }
  b->insns[b->len++] = insn;
  return 0;
}

/* match: dst v4 addr at addr_off masked; on match falls through to the RET+accept right below, on mismatch jf=1 skips it to the next clause */
static int emit_v4_clause(bpf_buf_t *b, unsigned addr_off, const cidr_t *c) {
  uint32_t mask = ntohl(c->u.v4.mask.s_addr);
  uint32_t net = ntohl(c->u.v4.addr.s_addr) & mask;
  if (bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, addr_off)) < 0)
    return -1;
  if (bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_ALU | BPF_AND | BPF_K, mask)) < 0)
    return -1;
  if (bpf_emit(b, (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, net, 0, 1)) < 0)
    return -1;
  return bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, 0xFFFFFFFFu));
}

/* prefix-derived mask for 32-bit word w (0-based) of a v6 address */
static uint32_t v6_word_mask(unsigned prefix, unsigned w) {
  unsigned word_bits = w * 32;
  if (prefix <= word_bits)
    return 0;
  if (prefix >= word_bits + 32)
    return 0xFFFFFFFFu;
  return 0xFFFFFFFFu << (word_bits + 32 - prefix);
}

/* 4 word-checks chained by fixed jf offsets (10/7/4/1) so each clause is self-contained regardless of range count - no cross-clause backpatching needed */
static int emit_v6_clause(bpf_buf_t *b, unsigned addr_off, const cidr_t *c) {
  unsigned w;
  for (w = 0; w < 4; w++) {
    uint32_t raw, mask, net;
    unsigned jf;
    memcpy(&raw, &c->u.v6.addr.s6_addr[w * 4], 4);
    mask = v6_word_mask(c->u.v6.prefix, w);
    net = ntohl(raw) & mask;
    jf = (w == 3) ? 1 : (3 - w) * 3 + 1;
    if (bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, addr_off + w * 4)) < 0)
      return -1;
    if (bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_ALU | BPF_AND | BPF_K, mask)) < 0)
      return -1;
    if (bpf_emit(b, (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, net, 0, jf)) < 0)
      return -1;
  }
  return bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, 0xFFFFFFFFu));
}

static size_t bpf_dispatch_len(size_t nv4, size_t nv6) {
  return 4 * nv4 + 13 * nv6 + 5;
}

/* assumes A = ethertype on entry; base is the ethernet-payload offset (14 = no vlan, 18 = one vlan tag already unwrapped) */
static int emit_dispatch_block(bpf_buf_t *b, unsigned base, const cidr_t *ranges, size_t range_count) {
  size_t i, nv4 = 0, nv6 = 0;
  unsigned v4_section_len;

  for (i = 0; i < range_count; i++)
    if (ranges[i].family == AF_INET)
      nv4++;
    else
      nv6++;
  v4_section_len = (unsigned)(4 * nv4 + 1);

  if (bpf_emit(b, (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETH_P_IP, 0, v4_section_len)) < 0)
    return -1;
  for (i = 0; i < range_count; i++)
    if (ranges[i].family == AF_INET && emit_v4_clause(b, base + 16, &ranges[i]) < 0)
      return -1;
  if (bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, 0)) < 0) /* v4, no range matched */
    return -1;

  if (bpf_emit(b, (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETH_P_IPV6, 1, 0)) < 0)
    return -1;
  if (bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, 0)) < 0) /* neither v4 nor v6 */
    return -1;

  for (i = 0; i < range_count; i++)
    if (ranges[i].family == AF_INET6 && emit_v6_clause(b, base + 24, &ranges[i]) < 0)
      return -1;
  return bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, 0)); /* v6, no range matched */
}

/* ethertype+vlan prologue, then two copies of the dispatch block (base 14 / base 18) - classic BPF has no
 * subroutines, so the with-vlan/without-vlan tail is duplicated rather than parameterized at runtime.
 * the base-14 block can be longer than 255 instructions (conditional jt/jf are 8-bit), so skipping over it
 * to reach the vlan path uses an unconditional BPF_JA trampoline instead of a direct conditional jump. */
struct sock_filter *capture_build_bpf(const cidr_t *ranges, size_t range_count, size_t *out_len) {
  bpf_buf_t b;
  size_t i, nv4 = 0, nv6 = 0, d_len, total;

  memset(&b, 0, sizeof b);
  for (i = 0; i < range_count; i++)
    if (ranges[i].family == AF_INET)
      nv4++;
    else
      nv6++;
  d_len = bpf_dispatch_len(nv4, nv6);
  total = 2 * d_len + 4;
  if (total > CAPTURE_BPF_MAX_INSNS)
    return NULL;

  if (bpf_emit(&b, (struct sock_filter)BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12)) < 0)
    goto fail;
  if (bpf_emit(&b, (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETH_P_8021Q, 0, 1)) < 0)
    goto fail;
  if (bpf_emit(&b, (struct sock_filter)BPF_STMT(BPF_JMP | BPF_JA, (uint32_t)d_len)) < 0)
    goto fail;
  if (emit_dispatch_block(&b, 14, ranges, range_count) < 0)
    goto fail;
  if (bpf_emit(&b, (struct sock_filter)BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 16)) < 0)
    goto fail;
  if (emit_dispatch_block(&b, 18, ranges, range_count) < 0)
    goto fail;

  *out_len = b.len;
  return b.insns;
fail:
  free(b.insns);
  return NULL;
}

capture_t *capture_open(const char *iface, const char *const *ranges, size_t range_count, char *errbuf, size_t errbuf_len) {
  capture_t *cap;
  struct ifreq ifr;
  int ifindex;
  int ver;
  struct tpacket_req3 req;
  struct sockaddr_ll sll;
  struct packet_mreq mreq;
  struct sock_filter *prog = NULL;
  size_t prog_len = 0;
  struct sock_fprog fprog;
  size_t i;

  if (!iface) {
    snprintf(errbuf, errbuf_len, "capture interface required");
    return NULL;
  }
  if (strlen(iface) >= IFNAMSIZ) {
    char shown[IFNAMSIZ];
    memcpy(shown, iface, sizeof shown - 1);
    shown[sizeof shown - 1] = '\0';
    snprintf(errbuf, errbuf_len, "%s: interface name too long", shown);
    return NULL;
  }

  cap = calloc(1, sizeof *cap);
  if (!cap) {
    snprintf(errbuf, errbuf_len, "out of memory");
    return NULL;
  }
  cap->fd = -1;

  cap->parsed_ranges = calloc(range_count, sizeof *cap->parsed_ranges);
  if (!cap->parsed_ranges) {
    snprintf(errbuf, errbuf_len, "out of memory");
    goto fail;
  }
  cap->range_count = range_count;
  for (i = 0; i < range_count; i++) {
    if (cidr_parse(ranges[i], &cap->parsed_ranges[i]) != 0) {
      snprintf(errbuf, errbuf_len, "invalid range: %s", ranges[i]);
      goto fail;
    }
  }

  cap->fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
  if (cap->fd < 0) {
    if (errno == EPERM)
      snprintf(errbuf, errbuf_len, "capture needs CAP_NET_RAW (setcap cap_net_raw+ep on the binary, or run as root and use -u to drop privileges after opening)");
    else
      snprintf(errbuf, errbuf_len, "socket: %s", strerror(errno));
    goto fail;
  }

  memset(&ifr, 0, sizeof ifr);
  strncpy(ifr.ifr_name, iface, sizeof ifr.ifr_name - 1);
  if (ioctl(cap->fd, SIOCGIFINDEX, &ifr) < 0) {
    snprintf(errbuf, errbuf_len, "%s: %s", iface, strerror(errno));
    goto fail;
  }
  ifindex = ifr.ifr_ifindex;

  memset(&ifr, 0, sizeof ifr);
  strncpy(ifr.ifr_name, iface, sizeof ifr.ifr_name - 1);
  if (ioctl(cap->fd, SIOCGIFHWADDR, &ifr) < 0) {
    snprintf(errbuf, errbuf_len, "%s: %s", iface, strerror(errno));
    goto fail;
  }
  if (ifr.ifr_hwaddr.sa_family != ARPHRD_ETHER) {
    snprintf(errbuf, errbuf_len, "%s: not an Ethernet interface", iface);
    goto fail;
  }

  ver = TPACKET_V3;
  if (setsockopt(cap->fd, SOL_PACKET, PACKET_VERSION, &ver, sizeof ver) < 0) {
    snprintf(errbuf, errbuf_len, "PACKET_VERSION: %s", strerror(errno));
    goto fail;
  }

  memset(&req, 0, sizeof req);
  req.tp_block_size = CAPTURE_TP_BLOCK_SIZE;
  req.tp_frame_size = CAPTURE_TP_FRAME_SIZE;
  req.tp_block_nr = CAPTURE_TP_BLOCK_NR;
  req.tp_frame_nr = (req.tp_block_size / req.tp_frame_size) * req.tp_block_nr;
  req.tp_retire_blk_tov = CAPTURE_TP_RETIRE_TOV_MS;
  if (setsockopt(cap->fd, SOL_PACKET, PACKET_RX_RING, &req, sizeof req) < 0) {
    snprintf(errbuf, errbuf_len, "PACKET_RX_RING: %s", strerror(errno));
    goto fail;
  }
  cap->block_size = req.tp_block_size;
  cap->block_nr = req.tp_block_nr;
  cap->ring_size = (size_t)req.tp_block_size * req.tp_block_nr;

  cap->ring = mmap(NULL, cap->ring_size, PROT_READ | PROT_WRITE, MAP_SHARED, cap->fd, 0);
  if (cap->ring == MAP_FAILED) {
    cap->ring = NULL;
    snprintf(errbuf, errbuf_len, "mmap: %s", strerror(errno));
    goto fail;
  }

  memset(&sll, 0, sizeof sll);
  sll.sll_family = AF_PACKET;
  sll.sll_protocol = htons(ETH_P_ALL);
  sll.sll_ifindex = ifindex;
  if (bind(cap->fd, (struct sockaddr *)&sll, sizeof sll) < 0) {
    snprintf(errbuf, errbuf_len, "bind %s: %s", iface, strerror(errno));
    goto fail;
  }

  memset(&mreq, 0, sizeof mreq);
  mreq.mr_ifindex = ifindex;
  mreq.mr_type = PACKET_MR_PROMISC;
  if (setsockopt(cap->fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof mreq) < 0) {
    snprintf(errbuf, errbuf_len, "promiscuous mode: %s", strerror(errno));
    goto fail;
  }

  prog = capture_build_bpf(cap->parsed_ranges, cap->range_count, &prog_len);
  if (!prog) {
    snprintf(errbuf, errbuf_len, "failed to build capture filter (too many -g ranges, or out of memory)");
    goto fail;
  }
  fprog.len = (unsigned short)prog_len;
  fprog.filter = prog;
  if (setsockopt(cap->fd, SOL_SOCKET, SO_ATTACH_FILTER, &fprog, sizeof fprog) < 0) {
    snprintf(errbuf, errbuf_len, "SO_ATTACH_FILTER: %s", strerror(errno));
    goto fail;
  }
  free(prog);
  return cap;

fail:
  free(prog);
  if (cap->ring)
    munmap(cap->ring, cap->ring_size);
  if (cap->fd >= 0)
    close(cap->fd);
  free(cap->parsed_ranges);
  free(cap);
  return NULL;
}

void capture_close(capture_t *cap) {
  if (!cap)
    return;
  if (cap->ring)
    munmap(cap->ring, cap->ring_size);
  if (cap->fd >= 0)
    close(cap->fd);
  free(cap->parsed_ranges);
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

void capture_handle_frame(const unsigned char *pkt, size_t len, const cidr_t *ranges, size_t range_count, capture_frame_cb cb, void *user) {
  size_t off, ip_off, udp_off, rtp_off, addr_len;
  unsigned ethertype, dport;
  int family;
  struct in_addr dst4;
  struct in6_addr dst6;
  const void *dst_bytes;
  rtp_hdr_t rtp;

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
    addr_len = sizeof dst4;
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
    addr_len = sizeof dst6;
    udp_off = hdr_off;
  } else {
    return; /* not IPv4 or IPv6 */
  }

  if (!in_ranges(family, dst_bytes, ranges, range_count)) /* userspace whitelist, authoritative regardless of the installed kernel filter */
    return;

  dport = ((unsigned)pkt[udp_off + 2] << 8) | pkt[udp_off + 3];
  rtp_off = udp_off + 8;
  if (rtp_off > len)
    return;

  if (rtp_payload_offset(pkt + rtp_off, len - rtp_off) == 0) /* not RTP-wrapped TS */
    return;
  if (!rtp_parse_header(pkt + rtp_off, len - rtp_off, &rtp))
    return;

  if (!cb)
    return;
  cb(family, dst_bytes, addr_len, dport, rtp.ssrc, rtp.seq, rtp.timestamp, pkt + rtp_off + rtp.payload_off, len - rtp_off - rtp.payload_off, user);
}

static void capture_drain_ring(capture_t *cap, capture_frame_cb cb, void *user) {
  for (;;) {
    struct tpacket_block_desc *bd = (struct tpacket_block_desc *)(cap->ring + cap->block_idx * cap->block_size);
    struct tpacket3_hdr *ppd;
    unsigned i;

    if (!(bd->hdr.bh1.block_status & TP_STATUS_USER))
      break;

    ppd = (struct tpacket3_hdr *)((unsigned char *)bd + bd->hdr.bh1.offset_to_first_pkt);
    for (i = 0; i < bd->hdr.bh1.num_pkts; i++) {
      const unsigned char *pkt = (const unsigned char *)ppd + ppd->tp_mac;
      capture_handle_frame(pkt, ppd->tp_snaplen, cap->parsed_ranges, cap->range_count, cb, user);
      ppd = (struct tpacket3_hdr *)((unsigned char *)ppd + ppd->tp_next_offset);
    }

    bd->hdr.bh1.block_status = TP_STATUS_KERNEL;
    cap->block_idx = (cap->block_idx + 1) % cap->block_nr;
  }
}

void capture_run(capture_t *cap, capture_frame_cb cb, void *user) {
  struct pollfd pfd;
  pfd.fd = cap->fd;
  pfd.events = POLLIN;

  while (!signal_stop_requested()) {
    int n = poll(&pfd, 1, CAPTURE_POLL_TIMEOUT_MS);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (n == 0)
      continue;
    capture_drain_ring(cap, cb, user);
  }
}
