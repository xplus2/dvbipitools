/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"

#include "../../version.h"

size_t g_capture_ring_cap = 4 * 1024 * 1024;
capture_ctx_t *g_open;
tsinspect_agg_t *g_insp_agg;
pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
_Atomic(capture_snapshot_t *) g_snapshot;
capture_snapshot_t *g_retired;

static int iface_eq(const char *a, const char *b) {
  if (!a && !b)
    return 1;
  if (!a || !b)
    return 0;
  return strcmp(a, b) == 0;
}

static capture_ctx_t *find_existing(int family, const char *group, const char *source, unsigned port, const char *iface) {
  for (capture_ctx_t *c = g_open; c; c = c->next)
    if (c->backend == CAP_BACKEND_MCAST && c->family == family && c->port == port &&
        strcmp(c->group, group) == 0 && strcmp(c->source, source) == 0 && iface_eq(c->iface, iface))
      return c;
  return NULL;
}

capture_ctx_t *find_existing_tssrc(const char *key) {
  for (capture_ctx_t *c = g_open; c; c = c->next)
    if (c->backend == CAP_BACKEND_TSSRC && c->key && !strcmp(c->key, key))
      return c;
  return NULL;
}

#define SNAPSHOT_SLACK 8

static capture_snapshot_t *snapshot_alloc(int need) {
  capture_snapshot_t *snap;
  int cap = need + SNAPSHOT_SLACK;
  snap = malloc(sizeof *snap + sizeof(capture_ctx_t *) * (size_t)cap);
  if (snap) snap->cap = cap;
  return snap;
}

static int open_count(void) {
  int n = 0;
  for (capture_ctx_t *c = g_open; c; c = c->next) n++;
  return n;
}

/* caller holds g_lock. snap->cap >= open_count(). consumes snap */
static void publish_snapshot(capture_snapshot_t *snap) {
  capture_ctx_t *c;
  capture_snapshot_t *old;
  int shard_n[CAPTURE_PUMP_MAX_THREADS] = {0};
  int cursor[CAPTURE_PUMP_MAX_THREADS];
  int n = 0;
  int off = 0;

  for (c = g_open; c; c = c->next) {
    n++;
    shard_n[c->pump_shard]++;
  }
  snap->n = n;
  for (int i = 0; i < CAPTURE_PUMP_MAX_THREADS; i++) {
    snap->shard_start[i] = off;
    snap->shard_count[i] = shard_n[i];
    cursor[i] = off;
    off += shard_n[i];
  }
  for (c = g_open; c; c = c->next) snap->ctxs[cursor[c->pump_shard]++] = c;
  old = atomic_exchange_explicit(&g_snapshot, snap, memory_order_acq_rel);
  if (old) {
    old->retired_next = g_retired;
    g_retired = old;
  }
}

static void free_snapshots(capture_snapshot_t *list) {
  capture_snapshot_t *next;
  for (; list; list = next) {
    next = list->retired_next;
    free(list);
  }
}

static int pumps_joined(void);

/* not on a pump thread, no lock held: may block on capture_wait_pumps_quiescent() */
void reclaim_retired_snapshots(void) {
  capture_snapshot_t *list;
  pthread_mutex_lock(&g_lock);
  list = g_retired;
  g_retired = NULL;
  pthread_mutex_unlock(&g_lock);
  if (!list) return;
  if (!pumps_joined()) capture_wait_pumps_quiescent();
  free_snapshots(list);
}

static capture_ctx_t *find_dup(const capture_ctx_t *n) {
  if (n->backend == CAP_BACKEND_TSSRC) return n->key ? find_existing_tssrc(n->key) : NULL;
  return find_existing(n->family, n->group, n->source, n->port, n->iface);
}

/* c if linked, existing dup (ref taken) if raced, NULL on oom. caller frees c unless returned */
capture_ctx_t *link_ctx(capture_ctx_t *c) {
  capture_snapshot_t *snap = NULL;
  capture_ctx_t *dup;
  int need;
  for (;;) {
    pthread_mutex_lock(&g_lock);
    dup = find_dup(c);
    if (dup) {
      atomic_fetch_add_explicit(&dup->refcount, 1, memory_order_relaxed);
      pthread_mutex_unlock(&g_lock);
      free(snap);
      return dup;
    }
    need = open_count() + 1;
    if (snap && snap->cap >= need) break;
    pthread_mutex_unlock(&g_lock);
    free(snap);
    snap = snapshot_alloc(need);
    if (!snap) return NULL;
  }
  c->next = g_open;
  g_open = c;
  publish_snapshot(snap);
  pthread_mutex_unlock(&g_lock);
  return c;
}

/* drop to 0 under g_lock: lookups inc under g_lock, cannot resurrect.
 * oom: ref kept and ctx stays linked, leak beats dangling snapshot entry */
int unref_unlink_ctx(capture_ctx_t *ctx) {
  capture_snapshot_t *snap = NULL;
  int need;
  for (;;) {
    pthread_mutex_lock(&g_lock);
    if (atomic_load_explicit(&ctx->refcount, memory_order_relaxed) > 1) {
      atomic_fetch_sub_explicit(&ctx->refcount, 1, memory_order_acq_rel);
      pthread_mutex_unlock(&g_lock);
      free(snap);
      return 0;
    }
    need = open_count();
    if (snap && snap->cap >= need) break;
    pthread_mutex_unlock(&g_lock);
    free(snap);
    snap = snapshot_alloc(need);
    if (!snap) return 0;
  }
  if (atomic_fetch_sub_explicit(&ctx->refcount, 1, memory_order_acq_rel) != 1) {
    pthread_mutex_unlock(&g_lock);
    free(snap);
    return 0;
  }
  for (capture_ctx_t **pp = &g_open; *pp; pp = &(*pp)->next) {
    if (*pp == ctx) {
      *pp = ctx->next;
      break;
    }
  }
  publish_snapshot(snap);
  pthread_mutex_unlock(&g_lock);
  return 1;
}

void capture_set_inspect(tsinspect_agg_t *agg) {
  g_insp_agg = agg;
}

void free_ctx_resources(capture_ctx_t *ctx) {
  if (ctx->insp) tsinspect_agg_remove(g_insp_agg, ctx->insp);
  if (ctx->backend == CAP_BACKEND_TSSRC) {
    tssrc_close(ctx->ts);
    free(ctx->key);
  } else {
    if (ctx->fcc) fcc_client_close(ctx->fcc);
    if (ctx->ret) ret_client_close(ctx->ret);
    if (ctx->fec_dec) fec2022_dec_free(ctx->fec_dec);
    if (ctx->fec_m) mcast_close(ctx->fec_m);
    mcast_close(ctx->m);
    free(ctx->iface);
  }
  free(ctx->ring);
  free(ctx);
}

ssize_t capture_fec_read(capture_ctx_t *ctx, unsigned char *buf, size_t cap) {
  unsigned char repair_buf[FEC2022_MAX_REPAIR];
  ssize_t rn = mcast_recv(ctx->fec_m, repair_buf, sizeof repair_buf, NULL);
  ssize_t n;
  if (rn > 0) fec2022_dec_repair(ctx->fec_dec, repair_buf, (size_t)rn);
  n = mcast_recv(ctx->m, buf, cap, NULL);
  if (n < 0) return -1;
  if (n > 0) fec2022_dec_source(ctx->fec_dec, buf, (size_t)n);
  return (ssize_t)fec2022_dec_drain(ctx->fec_dec, buf, cap);
}

void capture_set_ring_cap(size_t bytes) {
  if (bytes) g_capture_ring_cap = bytes;
}

static int addr_family(const char *addr) { return strchr(addr, ':') ? AF_INET6 : AF_INET; }

static ret_client_t *open_ret(const sds_ret_t *ret, int family, const char *group, unsigned port, const char *iface) {
  ret_client_cfg_t rc;
  ret_client_t *r;
  memset(&rc, 0, sizeof rc);
  rc.family = addr_family(ret->addr);
  bufcpy(rc.addr, sizeof rc.addr, ret->addr);
  rc.port = ret->port;
  rc.mc_enabled = ret->mc;
  rc.mc_port = ret->mc_port;
  rc.rtx_pt = ret->rtx_pt;
  rc.wait_ms = ret->rtx_time_ms;
  rc.source_family = family;
  bufcpy(rc.source_group, sizeof rc.source_group, group);
  rc.source_port = port;
  rc.iface_in = iface;
  r = ret_client_open(&rc);
  if (!r) log_code("YA600153", TOOL_NAME ": ret server %s:%u unreachable", ret->addr, ret->port);
  return r;
}

static void open_fec(capture_ctx_t *c, const sds_fec_t *fec, unsigned al_fec_l, unsigned al_fec_d, const char *iface) {
  c->fec_m = mcast_open(addr_family(fec->addr), fec->addr, fec->port, iface, 0);
  c->fec_dec = c->fec_m ? fec2022_dec_new(al_fec_l, al_fec_d) : NULL;
  if (!c->fec_m || !c->fec_dec || mcast_set_nonblock(c->fec_m)) {
    if (c->fec_dec) fec2022_dec_free(c->fec_dec);
    if (c->fec_m) mcast_close(c->fec_m);
    c->fec_m = NULL;
    c->fec_dec = NULL;
    log_code("YA600154", TOOL_NAME ": al-fec repair stream %s:%u unreachable", fec->addr, fec->port);
  }
}

static fcc_client_t *open_fcc(const sds_fcc_t *fcc) {
  fcc_client_cfg_t fc;
  fcc_client_t *f;
  memset(&fc, 0, sizeof fc);
  fc.family = addr_family(fcc->addr);
  bufcpy(fc.addr, sizeof fc.addr, fcc->addr);
  fc.port = fcc->port;
  fc.rtx_pt = fcc->rtx_pt;
  f = fcc_client_open(&fc);
  if (!f) log_code("YA600155", TOOL_NAME ": fcc server %s:%u unreachable", fcc->addr, fcc->port);
  return f;
}

capture_ctx_t *capture_open(int family, const char *group, const char *source, unsigned port, const char *iface, int rtp, const sds_ret_t *ret, const sds_fcc_t *fcc, const sds_fec_t *fec, unsigned al_fec_l, unsigned al_fec_d) {
  capture_ctx_t *c, *dup;
  if (!source) source = "";
  pthread_mutex_lock(&g_lock);
  c = find_existing(family, group, source, port, iface);
  if (c) {
    atomic_fetch_add_explicit(&c->refcount, 1, memory_order_relaxed);
    pthread_mutex_unlock(&g_lock);
    return c;
  }
  pthread_mutex_unlock(&g_lock);
  if (strlen(group) >= sizeof c->group || strlen(source) >= sizeof c->source) return NULL;
  c = calloc(1, sizeof *c);
  if (!c) return NULL;
  atomic_init(&c->ts_push_head, -1);
  c->pump_shard = next_pump_shard();
  c->ring = malloc(g_capture_ring_cap);
  if (!c->ring) {
    free_ctx_resources(c);
    return NULL;
  }
  c->backend = CAP_BACKEND_MCAST;
  bufcpy(c->group, sizeof c->group, group);
  bufcpy(c->source, sizeof c->source, source);
  c->iface = iface ? strdup(iface) : NULL;
  c->family = family;
  c->port = port;
  c->rtp = rtp;
  c->m = mcast_open_src(family, group, port, source, iface, 0); /* blocking join, unlocked */
  if (!c->m || mcast_set_nonblock(c->m)) {
    free_ctx_resources(c);
    return NULL;
  }
  if (ret) c->ret = open_ret(ret, family, group, port, iface);
  if (fcc) c->fcc = open_fcc(fcc);
  if (fec && al_fec_l) open_fec(c, fec, al_fec_l, al_fec_d, iface);
  c->refcount = 1;
  dup = link_ctx(c);
  if (dup != c) free_ctx_resources(c);
  return dup;
}

#define LIFE_OPEN_WORKERS 4
#define LIFE_JOBS_MAX 1024
#define LIFE_REAP_POLL_MS 5
#define LIFE_IDLE_RECLAIM_MS 1000

typedef struct capture_retired {
  struct capture_retired *next;
  uint64_t mark[CAPTURE_PUMP_MAX_THREADS];
  capture_ctx_t *ctx; /* NULL: snapshots only */
  capture_snapshot_t *snaps;
} capture_retired_t;

struct capture_open_job {
  capture_open_job_t *next;
  int wake_tid;
  int srt;
  int family;
  unsigned port;
  int rtp;
  char addr[256]; /* group or SRT host */
  char source[64];
  char iface[64]; /* "" = kernel default */
  char key[400];
  sds_ret_t ret;
  sds_fcc_t fcc;
  sds_fec_t fec;
  int has_ret;
  int has_fcc;
  int has_fec;
  unsigned al_fec_l;
  unsigned al_fec_d;
  _Atomic int finished;
  int cancelled; /* g_life_lock */
  capture_ctx_t *result;
};

/* lock order: g_life_lock, then g_lock */
static pthread_mutex_t g_life_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_life_cond;
static int g_life_accepting;
static int g_life_stop;
static int g_life_stopped; /* stop ran: pumps already joined */
static int g_life_nthreads;
static pthread_t g_life_threads[LIFE_OPEN_WORKERS];
static void (*g_life_wake)(int tid);
static capture_open_job_t *g_jobs_head;
static int g_jobs_n;
static const char *g_running_key[LIFE_OPEN_WORKERS];
static capture_retired_t *g_retired_head;

static void retired_free(capture_retired_t *r) {
  if (r->ctx) free_ctx_resources(r->ctx);
  free_snapshots(r->snaps);
  free(r);
}

static int pumps_joined(void) {
  int stopped;
  pthread_mutex_lock(&g_life_lock);
  stopped = g_life_stopped;
  pthread_mutex_unlock(&g_life_lock);
  return stopped;
}

/* caller holds g_life_lock. 1: queued, retired snapshots ride along */
static int retired_push_locked(capture_retired_t *r) {
  if (!g_life_accepting) return 0;
  pthread_mutex_lock(&g_lock);
  r->snaps = g_retired;
  g_retired = NULL;
  pthread_mutex_unlock(&g_lock);
  capture_pump_mark(r->mark);
  r->next = g_retired_head;
  g_retired_head = r;
  pthread_cond_signal(&g_life_cond);
  return 1;
}

int retire_ctx(capture_ctx_t *ctx) {
  capture_retired_t *r = calloc(1, sizeof *r);
  int queued;
  if (!r) return 0;
  r->ctx = ctx;
  pthread_mutex_lock(&g_life_lock);
  queued = retired_push_locked(r);
  pthread_mutex_unlock(&g_life_lock);
  if (!queued) free(r);
  return queued;
}

static void retire_snapshots(void) {
  capture_retired_t *r;
  int pending;
  pthread_mutex_lock(&g_lock);
  pending = g_retired != NULL;
  pthread_mutex_unlock(&g_lock);
  if (!pending) return;
  r = calloc(1, sizeof *r);
  if (!r) return;
  pthread_mutex_lock(&g_life_lock);
  if (!retired_push_locked(r)) free(r);
  pthread_mutex_unlock(&g_life_lock);
}

/* 1: entries left */
static int reap_retired(void) {
  capture_retired_t *r;
  capture_retired_t *next;
  capture_retired_t *keep = NULL;
  capture_retired_t *keep_tail = NULL;
  pthread_mutex_lock(&g_life_lock);
  r = g_retired_head;
  g_retired_head = NULL;
  pthread_mutex_unlock(&g_life_lock);
  for (; r; r = next) {
    next = r->next;
    if (capture_pump_mark_passed(r->mark)) {
      retired_free(r);
      continue;
    }
    r->next = NULL;
    if (keep_tail) keep_tail->next = r;
    else keep = r;
    keep_tail = r;
  }
  if (!keep) return 0;
  pthread_mutex_lock(&g_life_lock);
  keep_tail->next = g_retired_head;
  g_retired_head = keep;
  pthread_mutex_unlock(&g_life_lock);
  return 1;
}

/* caller holds g_life_lock. first job whose key nobody is opening */
static capture_open_job_t **find_job_locked(void) {
  for (capture_open_job_t **pp = &g_jobs_head; *pp; pp = &(*pp)->next) {
    int busy = 0;
    for (int i = 0; i < LIFE_OPEN_WORKERS; i++)
      if (g_running_key[i] && !strcmp(g_running_key[i], (*pp)->key)) busy = 1;
    if (!busy) return pp;
  }
  return NULL;
}

/* after stop pumps are joined: free at once instead of waiting for quiescence */
static void lifecycle_close(capture_ctx_t *ctx) {
  if (!unref_unlink_ctx(ctx)) return;
  if (retire_ctx(ctx)) return;
  if (!pumps_joined()) capture_wait_pumps_quiescent();
  reclaim_retired_snapshots();
  free_ctx_resources(ctx);
}

static void open_job_run(capture_open_job_t *job, int slot, int skip) {
  capture_ctx_t *ctx = NULL;
  int tid;
  if (!skip) {
    if (job->srt) ctx = capture_open_srt(job->addr, job->port);
    else
      ctx = capture_open(job->family, job->addr, job->source, job->port, job->iface[0] ? job->iface : NULL, job->rtp, job->has_ret ? &job->ret : NULL, job->has_fcc ? &job->fcc : NULL,
                         job->has_fec ? &job->fec : NULL, job->al_fec_l, job->al_fec_d);
  }
  pthread_mutex_lock(&g_life_lock);
  g_running_key[slot] = NULL;
  pthread_cond_broadcast(&g_life_cond); /* same-key jobs runnable */
  if (job->cancelled) {
    pthread_mutex_unlock(&g_life_lock);
    free(job);
    if (ctx) lifecycle_close(ctx);
    return;
  }
  job->result = ctx;
  tid = job->wake_tid;
  atomic_store_explicit(&job->finished, 1, memory_order_release);
  pthread_mutex_unlock(&g_life_lock);
  if (g_life_wake) g_life_wake(tid);
}

static void life_wait_locked(int ms) {
  struct timespec ts;
  if (ms < 0) {
    pthread_cond_wait(&g_life_cond, &g_life_lock);
    return;
  }
  clock_gettime(CLOCK_MONOTONIC, &ts);
  ts.tv_sec += ms / 1000;
  ts.tv_nsec += (long)(ms % 1000) * 1000000L;
  if (ts.tv_nsec >= 1000000000L) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000L;
  }
  pthread_cond_timedwait(&g_life_cond, &g_life_lock, &ts);
}

static void *lifecycle_thread(void *arg) {
  int slot = (int)(intptr_t)arg;
  for (;;) {
    capture_open_job_t **pp;
    capture_open_job_t *job = NULL;
    int skip = 0;
    int again;
    pthread_mutex_lock(&g_life_lock);
    pp = find_job_locked();
    if (pp) {
      job = *pp;
      *pp = job->next;
      g_jobs_n--;
      g_running_key[slot] = job->key;
      skip = job->cancelled;
    }
    pthread_mutex_unlock(&g_life_lock);
    if (job) open_job_run(job, slot, skip);
    again = reap_retired();
    if (slot == 0) retire_snapshots();
    pthread_mutex_lock(&g_life_lock);
    if (g_life_stop) {
      pthread_mutex_unlock(&g_life_lock);
      break;
    }
    if (!find_job_locked()) {
      int wait_ms = -1;
      if (again) wait_ms = LIFE_REAP_POLL_MS;
      else if (slot == 0) wait_ms = LIFE_IDLE_RECLAIM_MS;
      life_wait_locked(wait_ms);
    }
    pthread_mutex_unlock(&g_life_lock);
  }
  return NULL;
}

int capture_lifecycle_start(void (*wake)(int tid)) {
  pthread_condattr_t ca;
  int n = 0;
  pthread_mutex_lock(&g_life_lock);
  if (g_life_accepting) {
    pthread_mutex_unlock(&g_life_lock);
    return 0;
  }
  pthread_condattr_init(&ca);
  pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
  pthread_cond_init(&g_life_cond, &ca);
  pthread_condattr_destroy(&ca);
  g_life_wake = wake;
  g_life_stop = 0;
  g_life_stopped = 0;
  g_life_accepting = 1;
  pthread_mutex_unlock(&g_life_lock);
  for (; n < LIFE_OPEN_WORKERS; n++)
    if (pthread_create(&g_life_threads[n], NULL, lifecycle_thread, (void *)(intptr_t)n) != 0) break;
  pthread_mutex_lock(&g_life_lock);
  g_life_nthreads = n;
  pthread_mutex_unlock(&g_life_lock);
  if (n > 0) return 0;
  capture_lifecycle_stop();
  pthread_mutex_lock(&g_life_lock);
  g_life_stopped = 0; /* pumps not joined by a failed start */
  pthread_mutex_unlock(&g_life_lock);
  return -1;
}

void capture_lifecycle_stop(void) {
  capture_open_job_t *job;
  capture_open_job_t *jnext;
  capture_retired_t *r;
  capture_retired_t *rnext;
  capture_snapshot_t *snaps;
  int n;
  pthread_mutex_lock(&g_life_lock);
  if (!g_life_accepting) {
    pthread_mutex_unlock(&g_life_lock);
    return;
  }
  g_life_accepting = 0;
  g_life_stopped = 1;
  g_life_stop = 1;
  n = g_life_nthreads;
  pthread_cond_broadcast(&g_life_cond);
  pthread_mutex_unlock(&g_life_lock);
  for (int i = 0; i < n; i++) pthread_join(g_life_threads[i], NULL);
  pthread_mutex_lock(&g_life_lock);
  job = g_jobs_head;
  g_jobs_head = NULL;
  g_jobs_n = 0;
  r = g_retired_head;
  g_retired_head = NULL;
  g_life_nthreads = 0;
  pthread_mutex_unlock(&g_life_lock);
  pthread_cond_destroy(&g_life_cond);
  pthread_mutex_lock(&g_lock);
  snaps = g_retired;
  g_retired = NULL;
  pthread_mutex_unlock(&g_lock);
  free_snapshots(snaps);
  /* Reactors and pumps must be joined before this. Else UAF on queued jobs */
  while (job) {
    jnext = job->next;
    free(job);
    job = jnext;
  }
  while (r) {
    rnext = r->next;
    retired_free(r);
    r = rnext;
  }
}

capture_ctx_t *capture_try_ref(int family, const char *group, const char *source, unsigned port, const char *iface) {
  capture_ctx_t *c;
  pthread_mutex_lock(&g_lock);
  c = find_existing(family, group, source ? source : "", port, iface);
  if (c) atomic_fetch_add_explicit(&c->refcount, 1, memory_order_relaxed);
  pthread_mutex_unlock(&g_lock);
  return c;
}

int capture_lifecycle_active(void) {
  int accepting;
  pthread_mutex_lock(&g_life_lock);
  accepting = g_life_accepting;
  pthread_mutex_unlock(&g_life_lock);
  return accepting;
}

static capture_open_job_t *job_submit(capture_open_job_t *job) {
  capture_open_job_t **pp;
  pthread_mutex_lock(&g_life_lock);
  if (!g_life_accepting || g_jobs_n >= LIFE_JOBS_MAX) {
    pthread_mutex_unlock(&g_life_lock);
    free(job);
    return NULL;
  }
  pp = &g_jobs_head;
  while (*pp) pp = &(*pp)->next;
  job->next = NULL;
  *pp = job;
  g_jobs_n++;
  pthread_cond_signal(&g_life_cond);
  pthread_mutex_unlock(&g_life_lock);
  return job;
}

capture_open_job_t *capture_open_async(int wake_tid, int family, const char *group, const char *source, unsigned port, const char *iface, int rtp, const sds_ret_t *ret, const sds_fcc_t *fcc, const sds_fec_t *fec,
                                       unsigned al_fec_l, unsigned al_fec_d) {
  capture_open_job_t *job;
  if (!source) source = "";
  if (strlen(group) >= sizeof job->addr || strlen(source) >= sizeof job->source) return NULL;
  if (iface && strlen(iface) >= sizeof job->iface) return NULL;
  job = calloc(1, sizeof *job);
  if (!job) return NULL;
  job->wake_tid = wake_tid;
  job->family = family;
  job->port = port;
  job->rtp = rtp;
  bufcpy(job->addr, sizeof job->addr, group);
  bufcpy(job->source, sizeof job->source, source);
  if (iface) bufcpy(job->iface, sizeof job->iface, iface);
  if (ret) {
    job->ret = *ret;
    job->has_ret = 1;
  }
  if (fcc) {
    job->fcc = *fcc;
    job->has_fcc = 1;
  }
  if (fec) {
    job->fec = *fec;
    job->has_fec = 1;
  }
  job->al_fec_l = al_fec_l;
  job->al_fec_d = al_fec_d;
  snprintf(job->key, sizeof job->key, "m|%d|%s|%s|%u|%s", family, job->addr, job->source, port, job->iface);
  return job_submit(job);
}

capture_open_job_t *capture_open_srt_async(int wake_tid, const char *host, unsigned port) {
  capture_open_job_t *job;
  if (strlen(host) >= sizeof job->addr) return NULL;
  job = calloc(1, sizeof *job);
  if (!job) return NULL;
  job->wake_tid = wake_tid;
  job->srt = 1;
  job->port = port;
  bufcpy(job->addr, sizeof job->addr, host);
  snprintf(job->key, sizeof job->key, "s|%s|%u", job->addr, port);
  return job_submit(job);
}

int capture_open_job_done(const capture_open_job_t *job) {
  return atomic_load_explicit(&job->finished, memory_order_acquire);
}

capture_ctx_t *capture_open_job_take(capture_open_job_t *job) {
  capture_ctx_t *ctx = job->result;
  free(job);
  return ctx;
}

void capture_open_job_cancel(capture_open_job_t *job) {
  int finished;
  pthread_mutex_lock(&g_life_lock);
  finished = atomic_load_explicit(&job->finished, memory_order_acquire);
  if (!finished) job->cancelled = 1;
  pthread_mutex_unlock(&g_life_lock);
  if (finished) capture_close(capture_open_job_take(job));
}

void capture_close(capture_ctx_t *ctx) {
  if (!ctx) return;
  if (!unref_unlink_ctx(ctx)) return;
  if (retire_ctx(ctx)) return;
  if (!pumps_joined()) capture_wait_pumps_quiescent();
  reclaim_retired_snapshots();
  free_ctx_resources(ctx);
}

int capture_active_count(void) {
  int n = 0;
  pthread_mutex_lock(&g_lock);
  for (capture_ctx_t *c = g_open; c; c = c->next) n++;
  pthread_mutex_unlock(&g_lock);
  return n;
}

uint64_t capture_bytes_total(void) {
  uint64_t total = 0;
  pthread_mutex_lock(&g_lock);
  for (capture_ctx_t *c = g_open; c; c = c->next) total += atomic_load_explicit(&c->write_total, memory_order_relaxed);
  pthread_mutex_unlock(&g_lock);
  return total;
}

uint64_t capture_ctx_bytes(const capture_ctx_t *ctx) {
  return atomic_load_explicit(&ctx->write_total, memory_order_acquire);
}

capture_ctx_t *capture_ref(capture_ctx_t *ctx) {
  atomic_fetch_add_explicit(&ctx->refcount, 1, memory_order_relaxed);
  return ctx;
}

_Atomic int *capture_ts_push_head_ptr(capture_ctx_t *ctx) { return &ctx->ts_push_head; }
_Atomic(void *) *capture_hls_seg_head_ptr(capture_ctx_t *ctx) { return &ctx->hls_seg_head; }
