/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include "../../reactor/qsbr.h"
#include "lib/helper/seqlock.h"
#include "lib/demux/rtp.h"
#include "lib/sys/signal.h"
#include "lib/helper/dateutil.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/epoll.h>
#include <string.h>
#include <time.h>

#define CAPTURE_TSSRC_DRAIN_BUDGET_MS 5
#define CAPTURE_MCAST_DRAIN_BUDGET_MS 5
#define CAPTURE_CLOCK_EVERY 8

static const capture_snapshot_t *g_pump_last_snap[CAPTURE_PUMP_MAX_THREADS];
static int g_pump_resume_idx[CAPTURE_PUMP_MAX_THREADS];

/* set once by capture_pump_set_thread_count() before any pump/worker thread starts, read-only after: plain int safe */
static int g_n_pump_threads = 1;

static qsbr_domain_t *g_pump_qsbr;

_Thread_local int t_pump_tid = -1;

static _Atomic int g_shard_ctr;

static struct {
  _Alignas(CACHELINE) _Atomic uint64_t v;
} g_shard_bytes[CAPTURE_PUMP_MAX_THREADS];

static void note_read(capture_ctx_t *ctx, ssize_t n) {
  atomic_fetch_add_explicit(&ctx->write_total, (uint64_t)n, memory_order_relaxed);
  atomic_fetch_add_explicit(&g_shard_bytes[ctx->pump_shard].v, (uint64_t)n, memory_order_relaxed);
}

uint64_t capture_bytes_total(void) {
  uint64_t total = 0;
  for (int i = 0; i < CAPTURE_PUMP_MAX_THREADS; i++) total += atomic_load_explicit(&g_shard_bytes[i].v, memory_order_relaxed);
  return total;
}

int next_pump_shard(void) {
  return atomic_fetch_add_explicit(&g_shard_ctr, 1, memory_order_relaxed) % g_n_pump_threads;
}

int capture_pump_thread_count(void) { return g_n_pump_threads; }

static int g_pump_epfd[CAPTURE_PUMP_MAX_THREADS];

void capture_pump_set_thread_count(int n) {
  if (n < 1) n = 1;
  if (n > CAPTURE_PUMP_MAX_THREADS) n = CAPTURE_PUMP_MAX_THREADS;
  g_n_pump_threads = n;
  g_pump_qsbr = qsbr_domain_create(n);
  for (int i = 0; i < CAPTURE_PUMP_MAX_THREADS; i++) g_pump_epfd[i] = i < n ? epoll_create1(EPOLL_CLOEXEC) : -1;
}

static void pump_watch_fd(int epfd, int fd) {
  struct epoll_event ev;
  if (epfd < 0 || fd < 0) return;
  memset(&ev, 0, sizeof ev);
  ev.events = EPOLLIN;
  epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
}

/* wakeup only, events carry no ctx: closed fds leave the set on their own */
void capture_pump_watch(const capture_ctx_t *c) {
  int epfd = g_pump_epfd[c->pump_shard];
  if (c->backend == CAP_BACKEND_MCAST) {
    pump_watch_fd(epfd, mcast_fd(c->m));
    if (c->fec_m) pump_watch_fd(epfd, mcast_fd(c->fec_m));
    return;
  }
  if (!tssrc_timer_driven(c->ts)) pump_watch_fd(epfd, tssrc_fd(c->ts));
  pump_watch_fd(epfd, tssrc_fec_fd(c->ts));
}

void capture_pump_wait(int pid, int max_ms) {
  struct epoll_event ev[16];
  int epfd = pid >= 0 && pid < CAPTURE_PUMP_MAX_THREADS ? g_pump_epfd[pid] : -1;
  qsbr_worker_offline(g_pump_qsbr, pid);
  if (epfd >= 0) {
    epoll_wait(epfd, ev, 16, max_ms);
  } else {
    struct timespec ts = {max_ms / 1000, (long)(max_ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
  }
  qsbr_worker_online(g_pump_qsbr, pid);
}

static int try_pin(capture_ctx_t *c) {
  int cur = atomic_load_explicit(&c->refcount, memory_order_relaxed);
  for (;;) {
    if (cur <= 0) return 0;
    if (atomic_compare_exchange_weak_explicit(&c->refcount, &cur, cur + 1, memory_order_acquire, memory_order_relaxed)) return 1;
  }
}

void capture_wait_pumps_quiescent(void) {
  uint64_t mark[CAPTURE_PUMP_MAX_THREADS];
  int64_t stop_deadline = 0;
  int spins = 0;
  qsbr_mark(g_pump_qsbr, mark);
  while (!qsbr_mark_passed_excl(g_pump_qsbr, mark, t_pump_tid)) {
    if (signal_stop_requested()) {
      if (!stop_deadline) stop_deadline = now_ms() + 2000;
      else if (now_ms() >= stop_deadline) break;
    }
    qsbr_backoff(spins++);
  }
}

void capture_pump_mark(uint64_t *mark) {
  qsbr_mark(g_pump_qsbr, mark);
}

int capture_pump_mark_passed(const uint64_t *mark) {
  return qsbr_mark_passed_excl(g_pump_qsbr, mark, t_pump_tid);
}

#define QSBR_DEFERRED_MAX 64

typedef struct {
  uint64_t mark[CAPTURE_PUMP_MAX_THREADS];
  qsbr_deferred_fn fn;
  void *arg;
  int active;
} qsbr_deferred_t;

static _Thread_local qsbr_deferred_t t_qsbr_deferred[QSBR_DEFERRED_MAX];
static _Thread_local int t_qsbr_deferred_active;

int capture_defer_after_quiescent(qsbr_deferred_fn fn, void *arg) {
  for (int i = 0; i < QSBR_DEFERRED_MAX; i++) {
    qsbr_deferred_t *d = &t_qsbr_deferred[i];
    if (d->active) continue;
    qsbr_mark(g_pump_qsbr, d->mark);
    d->fn = fn;
    d->arg = arg;
    d->active = 1;
    t_qsbr_deferred_active++;
    return 1;
  }
  return 0;
}

void capture_flush_deferred_quiescent(void) {
  if (t_qsbr_deferred_active <= 0) return;
  for (int i = 0; i < QSBR_DEFERRED_MAX; i++) {
    qsbr_deferred_t *d = &t_qsbr_deferred[i];
    if (!d->active) continue;
    if (!qsbr_mark_passed_excl(g_pump_qsbr, d->mark, t_pump_tid)) continue;
    d->active = 0;
    t_qsbr_deferred_active--;
    d->fn(d->arg);
  }
}

/* stdin/http aren't packet-aligned: reassembles 188B packets across calls */
/* poll-free reads: only POLLIN, nonblocking fd (not HTTP) */
static int tssrc_poll_each_read(const tssrc_t *ts, int fd) {
  int fl;
  if (tssrc_kind(ts) == TSSRC_HTTP || tssrc_poll_events(ts) != POLLIN) return 1;
  fl = fcntl(fd, F_GETFL);
  return fl < 0 || !(fl & O_NONBLOCK);
}

typedef enum { CAP_RD_FCC, CAP_RD_RET, CAP_RD_FEC, CAP_RD_PLAIN } cap_read_backend_t;

static ssize_t capture_read_dispatch(capture_ctx_t *ctx, unsigned char *buf, size_t bufcap, int *unwrapped) {
  ssize_t n;
  cap_read_backend_t backend = CAP_RD_PLAIN;
  if (ctx->fcc) backend = CAP_RD_FCC;
  else if (ctx->ret) backend = CAP_RD_RET;
  else if (ctx->fec_dec) backend = CAP_RD_FEC;
  *unwrapped = 0;
  switch (backend) {
    case CAP_RD_FCC:
      *unwrapped = 1;
      n = fcc_client_read(ctx->fcc, ctx->m, buf, bufcap, 0);
      if (fcc_client_done(ctx->fcc)) {
        fcc_client_close(ctx->fcc);
        ctx->fcc = NULL;
        if (!ctx->ret) mcast_set_batch(ctx->m, CAPTURE_MCAST_BATCH);
      }
      break;
    case CAP_RD_RET:
      *unwrapped = 1;
      n = ret_client_read(ctx->ret, ctx->m, buf, bufcap, 0);
      break;
    case CAP_RD_FEC:
      n = capture_fec_read(ctx, buf, bufcap);
      break;
    case CAP_RD_PLAIN:
      n = mcast_recv(ctx->m, buf, bufcap, NULL);
      break;
  }
  return n;
}

static int tssrc_drain(capture_ctx_t *ctx, capture_sink_fn sink, void *user) {
  int64_t deadline = now_ms() + CAPTURE_TSSRC_DRAIN_BUDGET_MS;
  int fec_fd = tssrc_fec_fd(ctx->ts);
  int total = 0;
  int reads = 0;
  int gate = -1;
  /* caps g_lock hold per tick: continuous source never returns 0 on its own */
  for (;;) {
    unsigned char buf[CAPTURE_RECV_BUF];
    size_t len;
    size_t off;
    size_t whole;
    ssize_t n;
    int fd;
    if ((reads++ & (CAPTURE_CLOCK_EVERY - 1)) == 0 && now_ms() >= deadline) return total;
    fd = tssrc_timer_driven(ctx->ts) ? -1 : tssrc_fd(ctx->ts);
    if (fd >= 0 && gate < 0) gate = tssrc_poll_each_read(ctx->ts, fd);
    if (fd >= 0 && (gate || reads == 1 || fec_fd >= 0)) {
      /* underlying fd (stdin above all) may be blocking with nothing to read: poll first, never call tssrc_read() blind */
      struct pollfd pfd[2];
      nfds_t nfds = 1;
      pfd[0].fd = fd;
      pfd[0].events = tssrc_poll_events(ctx->ts);
      pfd[0].revents = 0;
      if (fec_fd >= 0) {
        pfd[1].fd = fec_fd;
        pfd[1].events = POLLIN;
        pfd[1].revents = 0;
        nfds = 2;
      }
      if (poll(pfd, nfds, 0) <= 0) return total;
      if (nfds == 2 && pfd[1].revents) tssrc_fec_poll(ctx->ts);
      else if (!pfd[0].revents) continue;
    }
    n = tssrc_read(ctx->ts, buf, sizeof buf, NULL);
    if (n < 0 && !gate && (errno == EAGAIN || errno == EWOULDBLOCK)) return total;
    if (n < 0) {
      atomic_store_explicit(&ctx->dead, 1, memory_order_relaxed);
      return -1;
    }
    if (n == 0) return total;
    note_read(ctx, n);
    len = (size_t)n;
    off = 0;
    if (ctx->leftover_len) {
      size_t need = 188 - ctx->leftover_len;
      size_t take = need < len ? need : len;
      memcpy(ctx->leftover + ctx->leftover_len, buf, take);
      ctx->leftover_len += take;
      off += take;
      if (ctx->leftover_len < 188) continue;
      if (ctx->insp) tsinspect_packet(ctx->insp, ctx->leftover);
      sink(ctx, user, ctx->leftover, 1);
      total++;
      ctx->leftover_len = 0;
    }
    whole = (len - off) / 188;
    if (ctx->insp)
      for (size_t i = 0; i < whole; i++) tsinspect_packet(ctx->insp, buf + off + i * 188);
    if (whole) sink(ctx, user, buf + off, whole);
    total += (int)whole;
    off += whole * 188;
    if (off < len) {
      ctx->leftover_len = len - off;
      memcpy(ctx->leftover, buf + off, ctx->leftover_len);
    }
  }
}

int capture_drain(capture_ctx_t *ctx, capture_sink_fn sink, void *user) {
  int64_t deadline;
  int total = 0;
  if (g_insp_agg && !ctx->insp) ctx->insp = tsinspect_agg_add(g_insp_agg);
  if (ctx->insp) tsinspect_tick(ctx->insp, mono_seconds());
  if (ctx->backend == CAP_BACKEND_TSSRC) return tssrc_drain(ctx, sink, user);
  deadline = now_ms() + CAPTURE_MCAST_DRAIN_BUDGET_MS;
  for (int reads = 0;; reads++) {
    unsigned char buf[CAPTURE_RECV_BUF];
    const unsigned char *payload;
    size_t len;
    ssize_t n;
    int unwrapped;
    /* flooded group must not starve shard peers */
    if ((reads & (CAPTURE_CLOCK_EVERY - 1)) == 0 && now_ms() >= deadline) return total;
    n = capture_read_dispatch(ctx, buf, sizeof buf, &unwrapped);
    if (n < 0) return -1;
    if (n == 0) return total;
    note_read(ctx, n);
    if (unwrapped) {
      payload = buf;
      len = (size_t)n;
    } else if (ctx->rtp) {
      size_t roff = rtp_payload_offset(buf, (size_t)n);
      if (roff == 0) continue;
      payload = buf + roff;
      len = (size_t)n - roff;
    } else {
      payload = buf;
      len = (size_t)n;
    }
    if (ctx->insp) tsinspect_grid(ctx->insp, payload, len);
    if (len >= 188) {
      sink(ctx, user, payload, len / 188);
      total += (int)(len / 188);
    }
  }
}

/* generous bound on one pump thread's shard of open sources: excess just wait for next tick */
#define CAPTURE_PUMP_SNAPSHOT_MAX 4096

static void pump_release_pin(capture_ctx_t *c) {
  if (!unref_unlink_ctx(c)) return;
  if (retire_ctx(c)) return;
  free_ctx_resources(c); /* lifecycle down: inline, cannot wait on own quiescence */
}

int capture_pump_tick(int pid, capture_sink_fn sink, void *user) {
  capture_ctx_t *snap_pin[CAPTURE_PUMP_SNAPSHOT_MAX];
  int n = 0;
  int i;
  int total = 0;
  int idx;
  int start;
  int shard_off;
  int shard_cnt;
  const capture_snapshot_t *snap;

  snap = atomic_load_explicit(&g_snapshot, memory_order_acquire);
  if (snap != g_pump_last_snap[pid]) {
    g_pump_last_snap[pid] = snap;
    g_pump_resume_idx[pid] = 0;
  }
  if (snap) {
    shard_off = snap->shard_start[pid];
    shard_cnt = snap->shard_count[pid];
    start = g_pump_resume_idx[pid];
    for (idx = start; idx < shard_cnt && n < CAPTURE_PUMP_SNAPSHOT_MAX; idx++) {
      capture_ctx_t *c = snap->ctxs[shard_off + idx];
      if (!try_pin(c)) continue;
      snap_pin[n++] = c;
    }
    if (n < CAPTURE_PUMP_SNAPSHOT_MAX) /* end reached b4 cap: wrap to head */
      for (idx = 0; idx < start && n < CAPTURE_PUMP_SNAPSHOT_MAX; idx++) {
        capture_ctx_t *c = snap->ctxs[shard_off + idx];
        if (!try_pin(c)) continue;
        snap_pin[n++] = c;
      }
    g_pump_resume_idx[pid] = idx >= shard_cnt ? 0 : idx;
  }

  for (i = 0; i < n; i++) {
    int got = capture_drain(snap_pin[i], sink, user);
    if (got < 0) log_throttled(&snap_pin[i]->drain_err_throttle, LOG_THROTTLE_WINDOW_S, "YE000672", "capture: source read error");
    else total += got;
  }
  for (i = 0; i < n; i++) pump_release_pin(snap_pin[i]);
  qsbr_worker_quiescent(g_pump_qsbr, pid);
  return total;
}
