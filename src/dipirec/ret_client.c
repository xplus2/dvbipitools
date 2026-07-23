/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/demux/rtp.h"
#include "lib/demux/rtx.h"
#include "lib/log.h"
#include "lib/mux/rtcp_build.h"
#include "ret_client.h"

#define RET_GAP_MAX 32       /* missing seqs tracked per gap; a bigger one just resyncs, matching no-RET behavior */
#define RET_HOLD_CAP 1500    /* held slot size, one Ethernet MTU; a bigger datagram bypasses holding entirely */
#define RET_OUTQ_SLOTS (RET_GAP_MAX + 1)
#define RET_OUTQ_CAP 65536   /* must fit any real received datagram, unlike RET_HOLD_CAP */
#define RET_DSCP_RTCP (0x1A << 2) /* Annex F.9 signalling mark, mirrors dipiret/ret.h */

typedef struct {
  int used;
  size_t len;
  unsigned char data[RET_HOLD_CAP];
} ret_hold_t;

typedef struct {
  size_t len;
  unsigned char data[RET_OUTQ_CAP];
} ret_outq_t;

struct ret_client {
  int uni_fd;
  mcast_t *repair; /* NULL if --no-ret-mc */
  unsigned char rtx_pt;
  unsigned wait_ms;
  uint32_t sender_ssrc;

  int have_ssrc;
  uint32_t ssrc;
  uint16_t expected_seq; /* also the gap base while gap_pending */

  int gap_pending;
  size_t hold_n;
  double gap_deadline;
  ret_hold_t hold[RET_GAP_MAX];

  ret_outq_t outq[RET_OUTQ_SLOTS];
  size_t outq_head, outq_count;
};

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void outq_push(ret_client_t *r, const unsigned char *data, size_t len) {
  size_t idx;
  if (r->outq_count >= RET_OUTQ_SLOTS)
    return; /* can't happen per the call-site accounting, kept as a backstop */
  if (len > RET_OUTQ_CAP)
    len = RET_OUTQ_CAP;
  idx = (r->outq_head + r->outq_count) % RET_OUTQ_SLOTS;
  memcpy(r->outq[idx].data, data, len);
  r->outq[idx].len = len;
  r->outq_count++;
}

static ssize_t outq_pop(ret_client_t *r, unsigned char *buf, size_t cap) {
  ret_outq_t *s;
  size_t n;
  if (!r->outq_count)
    return 0;
  s = &r->outq[r->outq_head];
  n = s->len < cap ? s->len : cap;
  memcpy(buf, s->data, n);
  r->outq_head = (r->outq_head + 1) % RET_OUTQ_SLOTS;
  r->outq_count--;
  return (ssize_t)n;
}

/* hold[0] out, everything else down one slot, gap base advances */
static void gap_shift(ret_client_t *r) {
  size_t i;
  for (i = 0; i + 1 < r->hold_n; i++)
    r->hold[i] = r->hold[i + 1];
  if (r->hold_n)
    r->hold_n--;
  r->expected_seq++;
}

static void flush_ready(ret_client_t *r, double now) {
  while (r->gap_pending) {
    if (r->hold[0].used) {
      outq_push(r, r->hold[0].data, r->hold[0].len);
      gap_shift(r);
    } else if (now >= r->gap_deadline) {
      log_line("ret: seq %u lost, not repaired in time", (unsigned)r->expected_seq);
      gap_shift(r);
    } else {
      break;
    }
    if (!r->hold_n)
      r->gap_pending = 0;
  }
}

/* forces the gap closed now, ignoring the deadline; used on ssrc change or a disjoint new gap */
static void abandon_gap(ret_client_t *r) {
  while (r->gap_pending) {
    if (r->hold[0].used)
      outq_push(r, r->hold[0].data, r->hold[0].len);
    else
      log_line("ret: seq %u lost, gap abandoned", (unsigned)r->expected_seq);
    gap_shift(r);
    if (!r->hold_n)
      r->gap_pending = 0;
  }
}

static void send_nack(ret_client_t *r, uint16_t start, size_t missing) {
  rtcp_nack_entry_t entries[RTCP_NACK_MAX_ENTRIES];
  unsigned char pkt[12 + RTCP_NACK_MAX_ENTRIES * 4];
  size_t n = 0, done = 0, len;

  while (done < missing && n < RTCP_NACK_MAX_ENTRIES) {
    size_t chunk = missing - done, i;
    uint16_t blp = 0;
    if (chunk > 17)
      chunk = 17;
    for (i = 1; i < chunk; i++)
      blp = (uint16_t)(blp | (1u << (i - 1)));
    entries[n].pid = (uint16_t)(start + done);
    entries[n].blp = blp;
    n++;
    done += chunk;
  }
  len = rtcp_build_ff(r->sender_ssrc, r->ssrc, entries, n, pkt, sizeof pkt);
  if (len)
    send(r->uni_fd, pkt, len, 0);
}

static void handle_original_seq(ret_client_t *r, uint16_t seq, const unsigned char *payload, size_t len, double now) {
  for (;;) {
    if (!r->gap_pending) {
      int16_t sdelta = (int16_t)(seq - r->expected_seq);
      size_t missing;
      if (sdelta < 0)
        return; /* stale duplicate */
      missing = (size_t)sdelta;
      if (!missing) {
        outq_push(r, payload, len);
        r->expected_seq++;
        return;
      }
      if (missing > RET_GAP_MAX || len > RET_HOLD_CAP) {
        log_line("ret: gap of %zu too large to track, resyncing at seq %u", missing, (unsigned)seq);
        r->expected_seq = (uint16_t)(seq + 1);
        outq_push(r, payload, len);
        return;
      }
      r->gap_pending = 1;
      r->hold_n = missing + 1;
      memset(r->hold, 0, sizeof r->hold);
      r->hold[missing].used = 1;
      r->hold[missing].len = len;
      memcpy(r->hold[missing].data, payload, len);
      r->gap_deadline = now + (double)r->wait_ms / 1000.0;
      send_nack(r, r->expected_seq, missing);
      return;
    }

    {
      int16_t delta = (int16_t)(seq - r->expected_seq);
      if (delta < 0)
        return; /* stale duplicate */
      if ((size_t)delta < r->hold_n) {
        if (!r->hold[delta].used && len <= RET_HOLD_CAP) {
          r->hold[delta].used = 1;
          r->hold[delta].len = len;
          memcpy(r->hold[delta].data, payload, len);
        }
        return;
      }
      if ((size_t)delta == r->hold_n && r->hold_n < RET_GAP_MAX && len <= RET_HOLD_CAP) {
        r->hold[r->hold_n].used = 1;
        r->hold[r->hold_n].len = len;
        memcpy(r->hold[r->hold_n].data, payload, len);
        r->hold_n++;
        return;
      }
      abandon_gap(r); /* disjoint from the current window, or oversized: retry fresh below */
    }
  }
}

static void on_original(ret_client_t *r, const rtp_hdr_t *hdr, const unsigned char *payload, size_t len, double now) {
  if (!r->have_ssrc || hdr->ssrc != r->ssrc) {
    if (r->have_ssrc)
      log_line("ret: ssrc changed (0x%08x -> 0x%08x), resetting gap tracking", (unsigned)r->ssrc, (unsigned)hdr->ssrc);
    if (r->gap_pending)
      abandon_gap(r);
    r->have_ssrc = 1;
    r->ssrc = hdr->ssrc;
    r->expected_seq = hdr->seq;
  }
  handle_original_seq(r, hdr->seq, payload, len, now);
  flush_ready(r, now);
}

static void on_repair(ret_client_t *r, const unsigned char *pkt, size_t len, double now) {
  rtx_pkt_t rx;
  int16_t delta;

  if (!r->have_ssrc || !r->gap_pending)
    return;
  if (!rtx_parse(pkt, len, r->rtx_pt, &rx) || rx.ssrc != r->ssrc || rx.payload_len > RET_HOLD_CAP)
    return;
  delta = (int16_t)(rx.osn - r->expected_seq);
  if (delta < 0 || (size_t)delta >= r->hold_n || r->hold[delta].used)
    return;
  r->hold[delta].used = 1;
  r->hold[delta].len = rx.payload_len;
  memcpy(r->hold[delta].data, rx.payload, rx.payload_len);
  flush_ready(r, now);
}

static int uni_socket_open(const config_t *cfg) {
  int fd, on = 1, tos = RET_DSCP_RTCP;

  fd = socket(cfg->ret.family, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    log_line("ret: socket: %s", strerror(errno));
    return -1;
  }
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);

  if (cfg->ret.family == AF_INET) {
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)cfg->ret.port);
    if (inet_pton(AF_INET, cfg->ret.addr, &a.sin_addr) != 1) {
      log_line("ret: bad --ret address: %s", cfg->ret.addr);
      close(fd);
      return -1;
    }
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) {
      log_line("ret: connect: %s", strerror(errno));
      close(fd);
      return -1;
    }
    setsockopt(fd, IPPROTO_IP, IP_TOS, &tos, sizeof tos);
  } else {
    struct sockaddr_in6 a;
    memset(&a, 0, sizeof a);
    a.sin6_family = AF_INET6;
    a.sin6_port = htons((unsigned short)cfg->ret.port);
    if (inet_pton(AF_INET6, cfg->ret.addr, &a.sin6_addr) != 1) {
      log_line("ret: bad --ret address: %s", cfg->ret.addr);
      close(fd);
      return -1;
    }
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) {
      log_line("ret: connect: %s", strerror(errno));
      close(fd);
      return -1;
    }
    setsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof tos);
  }
  return fd;
}

ret_client_t *ret_client_open(const config_t *cfg) {
  ret_client_t *r = calloc(1, sizeof *r);
  if (!r)
    return NULL;

  r->uni_fd = uni_socket_open(cfg);
  if (r->uni_fd < 0) {
    free(r);
    return NULL;
  }

  if (cfg->ret.mc_enabled) {
    unsigned port = cfg->ret.mc_port ? cfg->ret.mc_port : cfg->source.port;
    r->repair = mcast_open_ssm(cfg->source.family, cfg->source.group, port, cfg->ret.addr, cfg->iface, 0);
    if (!r->repair) {
      close(r->uni_fd);
      free(r);
      return NULL;
    }
  }

  r->rtx_pt = cfg->ret.rtx_pt;
  r->wait_ms = cfg->ret.wait_ms;
  srand((unsigned)(time(NULL) ^ getpid()));
  r->sender_ssrc = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
  return r;
}

ssize_t ret_client_read(ret_client_t *r, mcast_t *main, unsigned char *buf, size_t cap) {
  struct pollfd pfd[3];
  int nfds = 0, mi, ui, ri = -1;
  int timeout_ms;
  double now;
  ssize_t n;

  n = outq_pop(r, buf, cap);
  if (n > 0)
    return n;

  now = mono();
  timeout_ms = 1000;
  if (r->gap_pending) {
    double remain_ms = (r->gap_deadline - now) * 1000.0;
    if (remain_ms < 0)
      remain_ms = 0;
    if (remain_ms < timeout_ms)
      timeout_ms = (int)remain_ms;
  }

  mi = nfds; pfd[nfds].fd = mcast_fd(main); pfd[nfds].events = POLLIN; nfds++;
  ui = nfds; pfd[nfds].fd = r->uni_fd; pfd[nfds].events = POLLIN; nfds++;
  if (r->repair) {
    ri = nfds; pfd[nfds].fd = mcast_fd(r->repair); pfd[nfds].events = POLLIN; nfds++;
  }

  if (poll(pfd, (nfds_t)nfds, timeout_ms) < 0) {
    if (errno == EINTR)
      return 0;
    log_line("ret: poll: %s", strerror(errno));
    return -1;
  }

  now = mono();

  if (pfd[mi].revents & POLLIN) {
    unsigned char raw[65536];
    ssize_t rn = mcast_recv(main, raw, sizeof raw);
    if (rn < 0)
      return -1;
    if (rn > 0) {
      rtp_hdr_t hdr;
      if (rtp_parse_header(raw, (size_t)rn, &hdr) && (size_t)rn > hdr.payload_off)
        on_original(r, &hdr, raw + hdr.payload_off, (size_t)rn - hdr.payload_off, now);
    }
  }

  if (pfd[ui].revents & POLLIN) {
    unsigned char raw[65536];
    ssize_t rn = recv(r->uni_fd, raw, sizeof raw, 0);
    if (rn > 0)
      on_repair(r, raw, (size_t)rn, now);
  }

  if (ri >= 0 && (pfd[ri].revents & POLLIN)) {
    unsigned char raw[65536];
    ssize_t rn = mcast_recv(r->repair, raw, sizeof raw);
    if (rn > 0)
      on_repair(r, raw, (size_t)rn, now);
  }

  flush_ready(r, now);
  return outq_pop(r, buf, cap);
}

void ret_client_close(ret_client_t *r) {
  if (!r)
    return;
  close(r->uni_fd);
  if (r->repair)
    mcast_close(r->repair);
  free(r);
}
