/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <poll.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "../../helper/log.h"

#include "source_priv.h"

#define JITTER_FILL_MAX 256
#define JITTER_RECV_CAP 2048 /* over JITBUF_MAX_DGRAM: oversize seen, not truncated */

static uint64_t mono_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int is_dgram_kind(tssrc_kind_t k) { return k == TSSRC_RTP || k == TSSRC_UDP; }

int tssrc_jitter_attach(tssrc_t *s, unsigned delay_ms) {
  struct epoll_event ev;
  s->tfd = -1;
  s->epfd = -1;
  s->jb = jitbuf_new(delay_ms, is_dgram_kind(s->kind) ? JITBUF_AUTO : JITBUF_FIFO);
  if (!s->jb) return -1;
  s->tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  s->epfd = epoll_create1(EPOLL_CLOEXEC);
  if (s->tfd < 0 || s->epfd < 0) return -1;
  memset(&ev, 0, sizeof ev);
  ev.events = EPOLLIN;
  ev.data.fd = tssrc_src_fd(s);
  if (epoll_ctl(s->epfd, EPOLL_CTL_ADD, ev.data.fd, &ev) < 0) return -1;
  ev.data.fd = s->tfd;
  if (epoll_ctl(s->epfd, EPOLL_CTL_ADD, s->tfd, &ev) < 0) return -1;
  if (s->fec_dec) {
    ev.data.fd = tssrc_fec_fd(s);
    if (epoll_ctl(s->epfd, EPOLL_CTL_ADD, ev.data.fd, &ev) < 0) return -1;
  }
  return 0;
}

static ssize_t jitter_recv(tssrc_t *s, unsigned char *buf, net_err_reason_t *reason_out) {
  if (is_dgram_kind(s->kind)) return mcast_recv(s->m, buf, JITTER_RECV_CAP, reason_out);
  return tssrc_raw_fd_read(tssrc_src_fd(s), buf, JITBUF_MAX_DGRAM, reason_out);
}

static void jitter_src_failed(tssrc_t *s, net_err_reason_t r) {
  s->jb_failed = 1;
  s->jb_reason = r;
  epoll_ctl(s->epfd, EPOLL_CTL_DEL, tssrc_src_fd(s), NULL); /* dead fd stays readable */
}

static void jitter_fec_drain(tssrc_t *s, unsigned char *tmp, size_t cap, uint64_t now) {
  size_t n;
  while ((n = fec2022_dec_drain(s->fec_dec, tmp, cap)) > 0) jitbuf_push(s->jb, tmp, n, now);
}

static void jitter_fill(tssrc_t *s) {
  unsigned char tmp[JITTER_RECV_CAP];
  uint64_t expirations;
  ssize_t tr = read(s->tfd, &expirations, sizeof expirations);

  (void)tr;
  if (s->fec_dec) {
    tssrc_fec_poll(s);
    jitter_fec_drain(s, tmp, sizeof tmp, mono_ns());
  }
  for (unsigned i = 0; i < JITTER_FILL_MAX && !s->jb_failed; i++) {
    struct pollfd p;
    net_err_reason_t r = NET_ERR_OTHER;
    ssize_t n;
    uint64_t now;

    p.fd = tssrc_src_fd(s);
    p.events = POLLIN;
    p.revents = 0;
    if (poll(&p, 1, 0) <= 0) break;
    n = jitter_recv(s, tmp, &r);
    if (n < 0) {
      jitter_src_failed(s, r);
      break;
    }
    if (n == 0) break;
    now = mono_ns();
    if (!s->fec_dec) {
      jitbuf_push(s->jb, tmp, (size_t)n, now);
      continue;
    }
    fec2022_dec_source(s->fec_dec, tmp, (size_t)n);
    tssrc_fec_poll(s);
    jitter_fec_drain(s, tmp, sizeof tmp, now);
  }
}

static void jitter_rearm(const tssrc_t *s, uint64_t now) {
  struct itimerspec its;
  int64_t ns = jitbuf_next_ns(s->jb, now);

  memset(&its, 0, sizeof its);
  if (ns >= 0) {
    if (ns == 0) ns = 1;
    its.it_value.tv_sec = ns / 1000000000LL;
    its.it_value.tv_nsec = ns % 1000000000LL;
  }
  timerfd_settime(s->tfd, 0, &its, NULL);
}

ssize_t tssrc_jitter_read(tssrc_t *s, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  uint64_t now;
  size_t n;

  jitter_fill(s);
  now = mono_ns();
  n = jitbuf_pop(s->jb, buf, cap, now);
  jitter_rearm(s, now);
  if (n) return is_dgram_kind(s->kind) ? tssrc_rtp_strip(buf, n) : (ssize_t)n;
  if (s->jb_failed && jitbuf_queued(s->jb) == 0) {
    if (reason_out) *reason_out = s->jb_reason;
    return -1;
  }
  return 0;
}

void tssrc_jitter_free(tssrc_t *s) {
  jitbuf_stats_t st;
  if (!s->jb) return;
  jitbuf_stats(s->jb, &st);
  if (st.reordered || st.lost || st.late || st.dup || st.resync || st.dropped)
    log_line("jitter buffer: reordered %llu lost %llu late %llu dup %llu resync %llu dropped %llu", (unsigned long long)st.reordered,
             (unsigned long long)st.lost, (unsigned long long)st.late, (unsigned long long)st.dup, (unsigned long long)st.resync,
             (unsigned long long)st.dropped);
  jitbuf_free(s->jb);
  if (s->tfd >= 0) close(s->tfd);
  if (s->epfd >= 0) close(s->epfd);
}
int64_t tssrc_buffer_ms(const tssrc_t *s) {
  if (!s->jb) return -1;
  return (int64_t)(jitbuf_depth_ns(s->jb, mono_ns()) / 1000000ULL);
}
