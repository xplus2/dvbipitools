/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

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

#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include "lib/signal.h"

#include "capture.h"

#define CAPTURE_TP_BLOCK_SIZE (1u << 20) /* 1 MiB, page-multiple */
#define CAPTURE_TP_BLOCK_NR 64u          /* 64 MiB ring total */
#define CAPTURE_TP_FRAME_SIZE 2048u      /* covers 1500 MTU + 1 vlan tag + tpacket3_hdr */
#define CAPTURE_TP_RETIRE_TOV_MS 60u     /* block handed to userspace even under light traffic */
#define CAPTURE_POLL_TIMEOUT_MS 100

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
  struct passwd pwbuf, *pw;
  char *buf;
  long bufsize;
  int rc;
  if (!user)
    return 0;
  bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (bufsize <= 0)
    bufsize = 16384;
  buf = malloc((size_t)bufsize);
  if (!buf)
    return -1;
  rc = getpwnam_r(user, &pwbuf, buf, (size_t)bufsize, &pw);
  if (rc != 0 || !pw) {
    free(buf);
    return -1;
  }
  if (setgid(pw->pw_gid) < 0) {
    free(buf);
    return -1;
  }
  if (initgroups(pw->pw_name, pw->pw_gid) < 0) {
    free(buf);
    return -1;
  }
  if (setuid(pw->pw_uid) < 0) {
    free(buf);
    return -1;
  }
  free(buf);
  return 0;
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
