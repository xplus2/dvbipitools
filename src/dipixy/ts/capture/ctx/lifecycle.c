/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dipixy/ts/capture/priv.h"
#include "lib/sys/ioutil.h"

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
  int http;
  int insecure_tls;
  char url[1024];
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

#define HTTP_FAIL_SLOTS 8
#define HTTP_FAIL_HOLD_MS 5000
static struct {
  uint32_t hash;
  int64_t until_ms;
} g_http_fail[HTTP_FAIL_SLOTS]; /* g_life_lock */
static int g_http_fail_next;

static void retired_free(capture_retired_t *r) {
  if (r->ctx) free_ctx_resources(r->ctx);
  free_snapshots(r->snaps);
  free(r);
}

int pumps_joined(void) {
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

static uint32_t key_hash(const char *s) {
  uint32_t h = 2166136261u;
  for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
  return h;
}

static void http_fail_note(const char *key) {
  pthread_mutex_lock(&g_life_lock);
  g_http_fail[g_http_fail_next].hash = key_hash(key);
  g_http_fail[g_http_fail_next].until_ms = now_ms() + HTTP_FAIL_HOLD_MS;
  g_http_fail_next = (g_http_fail_next + 1) % HTTP_FAIL_SLOTS;
  pthread_mutex_unlock(&g_life_lock);
}

/* caller holds g_life_lock */
static int http_failed_recently_locked(const char *key) {
  uint32_t h = key_hash(key);
  int64_t now = now_ms();
  for (int i = 0; i < HTTP_FAIL_SLOTS; i++)
    if (g_http_fail[i].hash == h && g_http_fail[i].until_ms > now) return 1;
  return 0;
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
    else if (job->http) {
      ctx = capture_open_http_static(job->url, job->insecure_tls);
      if (!ctx) http_fail_note(job->key);
    } else
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

capture_open_job_t *capture_open_http_async(int wake_tid, const char *url, int insecure_tls) {
  capture_open_job_t *job;
  int failed;
  if (strlen(url) >= sizeof job->url) return NULL;
  job = calloc(1, sizeof *job);
  if (!job) return NULL;
  job->wake_tid = wake_tid;
  job->http = 1;
  job->insecure_tls = insecure_tls;
  bufcpy(job->url, sizeof job->url, url);
  bufcpy(job->key + bufcpy(job->key, sizeof job->key, "h|"), sizeof job->key - 2, job->url);
  pthread_mutex_lock(&g_life_lock);
  failed = http_failed_recently_locked(job->key);
  pthread_mutex_unlock(&g_life_lock);
  if (failed) {
    free(job);
    return NULL;
  }
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
