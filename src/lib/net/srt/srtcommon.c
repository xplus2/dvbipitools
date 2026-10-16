/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <netdb.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"

#include "srtcommon.h"

static void srt_log_cb(void *opaque, int level, const char *file, int line, const char *area, const char *message) {
  (void)opaque;
  (void)file;
  (void)line;
  (void)area;
  if (level <= LOG_ERR)          log_code("LE700011", "srt: %s", message);
  else if (level <= LOG_WARNING) log_code("LW700011", "srt: %s", message);
  else                           log_code("LD700011", "srt: %s", message);
}

/* libsrt <= 1.5.4: startup() skips refcount once GC runs, first cleanup() destroys all sockets.
   a real pair per proc */
static pthread_mutex_t srt_ref_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned srt_refs;

int srtcommon_startup(void) {
  int rc = 0;

  pthread_mutex_lock(&srt_ref_lock);
  if (srt_refs == 0 && srt_startup() == SRT_ERROR)
    rc = -1;
  else
    srt_refs++;
  pthread_mutex_unlock(&srt_ref_lock);
  return rc;
}

void srtcommon_cleanup(void) {
  pthread_mutex_lock(&srt_ref_lock);
  if (srt_refs > 0 && --srt_refs == 0)
    srt_cleanup();
  pthread_mutex_unlock(&srt_ref_lock);
}

void srtcommon_open_logging(int verbose) {
  srt_setloglevel(verbose ? LOG_NOTICE : LOG_WARNING);
  srt_setlogflags(SRT_LOGF_DISABLE_TIME | SRT_LOGF_DISABLE_THREADNAME | SRT_LOGF_DISABLE_SEVERITY | SRT_LOGF_DISABLE_EOL);
  srt_setloghandler(NULL, srt_log_cb);
}

int srtcommon_apply_opts(SRTSOCKET s, const srtcommon_opts_t *o, int is_group, int timeo_optname, int timeo_ms) {
  int transtype = SRTT_LIVE;

  if (!is_group && srt_setsockopt(s, 0, SRTO_TRANSTYPE, &transtype, sizeof transtype) != 0)
    goto fail;
  if (o->passphrase && o->passphrase[0]) {
    if (srt_setsockopt(s, 0, SRTO_PASSPHRASE, o->passphrase, (int)strlen(o->passphrase)) != 0)
      goto fail;
    if (o->pbkeylen) {
      int kl = o->pbkeylen;
      if (srt_setsockopt(s, 0, SRTO_PBKEYLEN, &kl, sizeof kl) != 0)
        goto fail;
    }
  }
  if (o->streamid && o->streamid[0] && srt_setsockopt(s, 0, SRTO_STREAMID, o->streamid, (int)strlen(o->streamid)) != 0)
    goto fail;
  if (o->packetfilter && o->packetfilter[0] &&
      srt_setsockopt(s, 0, SRTO_PACKETFILTER, o->packetfilter, (int)strlen(o->packetfilter)) != 0)
    goto fail;
  if (o->latency_ms) {
    int lat = (int)o->latency_ms;
    if (srt_setsockopt(s, 0, SRTO_LATENCY, &lat, sizeof lat) != 0)
      goto fail;
  }
  if (srt_setsockopt(s, 0, timeo_optname, &timeo_ms, sizeof timeo_ms) != 0)
    goto fail;
  return 0;

fail:
  log_code("LE500158", "srt: setsockopt failed: %s", srt_getlasterror_str());
  return -1;
}

int srtcommon_resolve(const char *host, unsigned port, struct sockaddr_storage *ss, int *len) {
  struct addrinfo hints;
  struct addrinfo *res;
  char portbuf[6];
  int rc;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  uint_to_str(portbuf, port);
  rc = getaddrinfo(host, portbuf, &hints, &res);
  if (rc != 0) {
    log_code("LE600135", "srt: address resolve failed for %s:%u: %s", host, port, gai_strerror(rc));
    return -1;
  }
  memcpy(ss, res->ai_addr, res->ai_addrlen);
  *len = (int)res->ai_addrlen;
  freeaddrinfo(res);
  return 0;
}

#define RESOLVE_INFLIGHT_MAX 16

static atomic_int g_resolve_inflight;

typedef struct {
  pthread_mutex_t mu;
  pthread_cond_t cv;
  int refs;
  int done;
  int rc;
  struct sockaddr_storage ss;
  int len;
  unsigned port;
  char host[256];
} resolve_job_t;

static void resolve_job_unref(resolve_job_t *j) {
  int n;
  pthread_mutex_lock(&j->mu);
  n = --j->refs;
  pthread_mutex_unlock(&j->mu);
  if (n) return;
  pthread_cond_destroy(&j->cv);
  pthread_mutex_destroy(&j->mu);
  free(j);
}

static void *resolve_thread(void *arg) {
  resolve_job_t *j = arg;
  struct sockaddr_storage ss;
  int len = 0;
  int rc = srtcommon_resolve(j->host, j->port, &ss, &len);
  pthread_mutex_lock(&j->mu);
  if (rc == 0) {
    memcpy(&j->ss, &ss, sizeof ss);
    j->len = len;
  }
  j->rc = rc;
  j->done = 1;
  pthread_cond_signal(&j->cv);
  pthread_mutex_unlock(&j->mu);
  atomic_fetch_sub_explicit(&g_resolve_inflight, 1, memory_order_relaxed);
  resolve_job_unref(j);
  return NULL;
}

/* NULL: failed. *sync_ok set: only thread creation failed, caller may resolve inline */
static resolve_job_t *resolve_job_start(const char *host, unsigned port, int *sync_ok) {
  resolve_job_t *j;
  pthread_condattr_t ca;
  pthread_attr_t ta;
  pthread_t th;
  int started;

  *sync_ok = 0;
  if (strlen(host) >= sizeof j->host) {
    *sync_ok = 1;
    return NULL;
  }
  if (atomic_fetch_add_explicit(&g_resolve_inflight, 1, memory_order_relaxed) >= RESOLVE_INFLIGHT_MAX) {
    atomic_fetch_sub_explicit(&g_resolve_inflight, 1, memory_order_relaxed);
    log_code("LE600135", "srt: address resolve failed for %s:%u: too many lookups in flight", host, port);
    return NULL;
  }
  j = calloc(1, sizeof *j);
  if (!j) {
    atomic_fetch_sub_explicit(&g_resolve_inflight, 1, memory_order_relaxed);
    return NULL;
  }
  pthread_mutex_init(&j->mu, NULL);
  pthread_condattr_init(&ca);
  pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
  pthread_cond_init(&j->cv, &ca);
  pthread_condattr_destroy(&ca);
  j->refs = 2;
  j->port = port;
  strcpy(j->host, host);
  pthread_attr_init(&ta);
  pthread_attr_setdetachstate(&ta, PTHREAD_CREATE_DETACHED);
  started = pthread_create(&th, &ta, resolve_thread, j) == 0;
  pthread_attr_destroy(&ta);
  if (!started) {
    atomic_fetch_sub_explicit(&g_resolve_inflight, 1, memory_order_relaxed);
    resolve_job_unref(j);
    resolve_job_unref(j);
    *sync_ok = 1;
    return NULL;
  }
  return j;
}

int srtcommon_resolve_stoppable(const char *host, unsigned port, struct sockaddr_storage *ss, int *len, unsigned timeout_ms, int (*stop)(void *), void *arg) {
  resolve_job_t *j;
  struct timespec t0;
  int sync_ok;
  int rc = -1;

  j = resolve_job_start(host, port, &sync_ok);
  if (!j) return sync_ok ? srtcommon_resolve(host, port, ss, len) : -1;

  clock_gettime(CLOCK_MONOTONIC, &t0);
  pthread_mutex_lock(&j->mu);
  while (!j->done) {
    struct timespec now;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (stop(arg)) break;
    if (timeout_ms && (now.tv_sec - t0.tv_sec) * 1000L + (now.tv_nsec - t0.tv_nsec) / 1000000L >= (long)timeout_ms) {
      log_code("LE600135", "srt: address resolve failed for %s:%u: timeout", host, port);
      break;
    }
    ts = now;
    ts.tv_nsec += 100000000L;
    if (ts.tv_nsec >= 1000000000L) {
      ts.tv_sec++;
      ts.tv_nsec -= 1000000000L;
    }
    pthread_cond_timedwait(&j->cv, &j->mu, &ts);
  }
  if (j->done && j->rc == 0) {
    memcpy(ss, &j->ss, sizeof *ss);
    *len = j->len;
    rc = 0;
  }
  pthread_mutex_unlock(&j->mu);
  resolve_job_unref(j);
  return rc;
}

struct srtcommon_resolver {
  int n;
  int pending;
  resolve_job_t *job[SRTCOMMON_MAX_PEERS + 1];
  int failed[SRTCOMMON_MAX_PEERS + 1];
  struct sockaddr_storage ss[SRTCOMMON_MAX_PEERS + 1];
  int len[SRTCOMMON_MAX_PEERS + 1];
};

srtcommon_resolver_t *srtcommon_resolver_start(const srtcommon_peer_t *peers, int n) {
  srtcommon_resolver_t *r;
  int sync_ok;

  if (n <= 0 || n > SRTCOMMON_MAX_PEERS + 1) return NULL;
  r = calloc(1, sizeof *r);
  if (!r) return NULL;
  r->n = n;
  for (int i = 0; i < n; i++) {
    r->job[i] = resolve_job_start(peers[i].host, peers[i].port, &sync_ok);
    if (r->job[i]) {
      r->pending++;
      continue;
    }
    /* inline fallback blocks, but only on thread/oversize-name failure */
    if (!sync_ok || srtcommon_resolve(peers[i].host, peers[i].port, &r->ss[i], &r->len[i])) r->failed[i] = 1;
  }
  return r;
}

int srtcommon_resolver_poll(srtcommon_resolver_t *r) {
  for (int i = 0; i < r->n; i++) {
    resolve_job_t *j = r->job[i];
    if (!j) continue;
    pthread_mutex_lock(&j->mu);
    if (!j->done) {
      pthread_mutex_unlock(&j->mu);
      continue;
    }
    if (j->rc == 0) {
      r->ss[i] = j->ss;
      r->len[i] = j->len;
    } else {
      r->failed[i] = 1;
    }
    pthread_mutex_unlock(&j->mu);
    resolve_job_unref(j);
    r->job[i] = NULL;
    r->pending--;
  }
  return r->pending == 0;
}

int srtcommon_resolver_get(const srtcommon_resolver_t *r, int i, struct sockaddr_storage *ss, int *len) {
  if (r->failed[i] || r->job[i]) return -1;
  *ss = r->ss[i];
  *len = r->len[i];
  return 0;
}

void srtcommon_resolver_free(srtcommon_resolver_t *r) {
  if (!r) return;
  for (int i = 0; i < r->n; i++)
    if (r->job[i]) resolve_job_unref(r->job[i]);
  free(r);
}

int srtcommon_build_group_config(const srtcommon_peer_t *peers, int npeers, SRT_SOCKGROUPCONFIG *gc_out) {
  for (int i = 0; i < npeers; i++) {
    struct sockaddr_storage remote;
    int remote_len;

    if (srtcommon_resolve(peers[i].host, peers[i].port, &remote, &remote_len))
      return -1;
    gc_out[i] = srt_prepare_endpoint(NULL, (struct sockaddr *)&remote, remote_len);
  }
  return 0;
}

static int addr_equal(const struct sockaddr_storage *a, const struct sockaddr_storage *b) {
  if (a->ss_family != b->ss_family)
    return 0;
  if (a->ss_family == AF_INET) {
    const struct sockaddr_in *x = (const struct sockaddr_in *)a;
    const struct sockaddr_in *y = (const struct sockaddr_in *)b;
    return x->sin_port == y->sin_port && x->sin_addr.s_addr == y->sin_addr.s_addr;
  }
  if (a->ss_family == AF_INET6) {
    const struct sockaddr_in6 *x = (const struct sockaddr_in6 *)a;
    const struct sockaddr_in6 *y = (const struct sockaddr_in6 *)b;
    return x->sin6_port == y->sin6_port && memcmp(&x->sin6_addr, &y->sin6_addr, sizeof x->sin6_addr) == 0;
  }
  return 0;
}

static int member_live(const SRT_SOCKGROUPDATA *m) {
  if (m->memberstate == SRT_GST_BROKEN)
    return 0;
  return m->sockstate != SRTS_BROKEN && m->sockstate != SRTS_CLOSING && m->sockstate != SRTS_CLOSED && m->sockstate != SRTS_NONEXIST;
}

int srtcommon_group_missing(const struct sockaddr_storage *addrs, int npeers, const SRT_SOCKGROUPDATA *data, size_t ndata, int *missing) {
  int n = 0;

  for (int i = 0; i < npeers; i++) {
    int found = 0;

    missing[i] = 0;
    if (addrs[i].ss_family == 0)
      continue;
    for (size_t j = 0; j < ndata && !found; j++)
      found = member_live(&data[j]) && addr_equal(&addrs[i], &data[j].peeraddr);
    if (!found) {
      missing[i] = 1;
      n++;
    }
  }
  return n;
}

static int group_live(SRTSOCKET g, SRT_SOCKGROUPDATA *data, size_t *ndata) {
  int live = 0;

  if (srt_group_data(g, data, ndata) != 0)
    return -1;
  for (size_t j = 0; j < *ndata; j++)
    live += member_live(&data[j]);
  return live;
}

int srtcommon_group_revive(SRTSOCKET g, const srtcommon_peer_t *peers, int npeers, srtcommon_resolver_t **res) {
  SRT_SOCKGROUPDATA data[SRTCOMMON_MAX_PEERS * 2];
  SRT_SOCKGROUPCONFIG gc[SRTCOMMON_MAX_PEERS];
  struct sockaddr_storage addrs[SRTCOMMON_MAX_PEERS];
  int missing[SRTCOMMON_MAX_PEERS];
  size_t ndata = sizeof data / sizeof data[0];
  int ng = 0;
  int old_rcv = 1;
  int old_snd = 1;
  int len = sizeof old_rcv;
  int no = 0;
  int live;
  int rc;

  if (!*res) {
    live = group_live(g, data, &ndata);
    if (live < 0 || live >= npeers)
      return 0;
    *res = srtcommon_resolver_start(peers, npeers);
    if (!*res)
      return 0;
  }
  if (!srtcommon_resolver_poll(*res))
    return 0;
  memset(addrs, 0, sizeof addrs);
  for (int i = 0; i < npeers; i++) {
    int alen;
    if (srtcommon_resolver_get(*res, i, &addrs[i], &alen))
      addrs[i].ss_family = 0;
  }
  srtcommon_resolver_free(*res);
  *res = NULL;
  ndata = sizeof data / sizeof data[0];
  if (group_live(g, data, &ndata) < 0)
    return 0;
  if (srtcommon_group_missing(addrs, npeers, data, ndata, missing) == 0)
    return 0;
  for (int i = 0; i < npeers; i++) {
    if (missing[i])
      gc[ng++] = srt_prepare_endpoint(NULL, (struct sockaddr *)&addrs[i], (int)sizeof addrs[i]);
  }
  /* group may be in sync mode: connect must not stall the data path */
  srt_getsockopt(g, 0, SRTO_RCVSYN, &old_rcv, &len);
  len = sizeof old_snd;
  srt_getsockopt(g, 0, SRTO_SNDSYN, &old_snd, &len);
  srt_setsockopt(g, 0, SRTO_RCVSYN, &no, sizeof no);
  srt_setsockopt(g, 0, SRTO_SNDSYN, &no, sizeof no);
  rc = srt_connect_group(g, gc, ng);
  srt_setsockopt(g, 0, SRTO_RCVSYN, &old_rcv, sizeof old_rcv);
  srt_setsockopt(g, 0, SRTO_SNDSYN, &old_snd, sizeof old_snd);
  if (rc == SRT_ERROR && srt_getlasterror(NULL) != SRT_EASYNCSND) {
    log_code("LW600165", "srt: group member re-add failed: %s", srt_getlasterror_str());
    return -1;
  }
  return 0;
}
