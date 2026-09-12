/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"

#include "../../version.h"

size_t g_capture_ring_cap = 4 * 1024 * 1024;
capture_ctx_t *g_open;
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

static capture_ctx_t *find_existing(int family, const char *group, unsigned port, const char *iface) {
  for (capture_ctx_t *c = g_open; c; c = c->next)
    if (c->backend == CAP_BACKEND_MCAST && c->family == family && c->port == port &&
        strcmp(c->group, group) == 0 && iface_eq(c->iface, iface))
      return c;
  return NULL;
}

capture_ctx_t *find_existing_tssrc(const char *key) {
  for (capture_ctx_t *c = g_open; c; c = c->next)
    if (c->backend == CAP_BACKEND_TSSRC && c->key && !strcmp(c->key, key))
      return c;
  return NULL;
}

/* caller holds g_lock */
void rebuild_snapshot(void) {
  capture_ctx_t *c;
  capture_snapshot_t *snap, *old;
  int n = 0;
  int shard_n[CAPTURE_PUMP_MAX_THREADS] = {0};
  int cursor[CAPTURE_PUMP_MAX_THREADS];
  int off;

  for (c = g_open; c; c = c->next) {
    n++;
    shard_n[c->pump_shard]++;
  }
  snap = malloc(sizeof *snap + sizeof(capture_ctx_t *) * (size_t)n);
  if (!snap)
    return;
  snap->n = n;
  off = 0;
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

/* not on a pump thread, no lock held: may block on capture_wait_pumps_quiescent() */
void reclaim_retired_snapshots(void) {
  capture_snapshot_t *list, *next;
  pthread_mutex_lock(&g_lock);
  list = g_retired;
  g_retired = NULL;
  pthread_mutex_unlock(&g_lock);
  if (!list) return;
  capture_wait_pumps_quiescent();
  for (; list; list = next) {
    next = list->retired_next;
    free(list);
  }
}

void unlink_ctx(capture_ctx_t *ctx) {
  pthread_mutex_lock(&g_lock);
  for (capture_ctx_t **pp = &g_open; *pp; pp = &(*pp)->next) if (*pp == ctx) {
    *pp = ctx->next;
    break;
  }
  rebuild_snapshot();
  pthread_mutex_unlock(&g_lock);
}

void free_ctx_resources(capture_ctx_t *ctx) {
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
  if (!r) log_line(TOOL_NAME ": ret server %s:%u unreachable", ret->addr, ret->port);
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
    log_line(TOOL_NAME ": al-fec repair stream %s:%u unreachable", fec->addr, fec->port);
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
  if (!f) log_line(TOOL_NAME ": fcc server %s:%u unreachable", fcc->addr, fcc->port);
  return f;
}

capture_ctx_t *capture_open(int family, const char *group, unsigned port, const char *iface, int rtp, const sds_ret_t *ret, const sds_fcc_t *fcc, const sds_fec_t *fec, unsigned al_fec_l, unsigned al_fec_d) {
  capture_ctx_t *c, *dup;
  pthread_mutex_lock(&g_lock);
  c = find_existing(family, group, port, iface);
  if (c) {
    atomic_fetch_add_explicit(&c->refcount, 1, memory_order_relaxed);
    pthread_mutex_unlock(&g_lock);
    return c;
  }
  pthread_mutex_unlock(&g_lock);
  if (strlen(group) >= sizeof c->group) return NULL;
  c = calloc(1, sizeof *c);
  if (!c) return NULL;
  atomic_init(&c->ts_push_head, -1);
  c->pump_shard = next_pump_shard();
  c->ring = malloc(g_capture_ring_cap);
  if (!c->ring) {
    free(c);
    return NULL;
  }
  c->backend = CAP_BACKEND_MCAST;
  bufcpy(c->group, sizeof c->group, group);
  c->iface = iface ? strdup(iface) : NULL;
  c->family = family;
  c->port = port;
  c->rtp = rtp;
  c->m = mcast_open(family, group, port, iface, 0); /* blocking join, unlocked */
  if (!c->m || mcast_set_nonblock(c->m)) {
    if (c->m) mcast_close(c->m);
    free(c->iface);
    free(c->ring);
    free(c);
    return NULL;
  }
  if (ret) c->ret = open_ret(ret, family, group, port, iface);
  if (fcc) c->fcc = open_fcc(fcc);
  if (fec && al_fec_l) open_fec(c, fec, al_fec_l, al_fec_d, iface);
  c->refcount = 1;
  pthread_mutex_lock(&g_lock);
  dup = find_existing(family, group, port, iface); /* raced another opener while unlocked */
  if (dup) {
    atomic_fetch_add_explicit(&dup->refcount, 1, memory_order_relaxed);
    pthread_mutex_unlock(&g_lock);
    if (c->fcc) fcc_client_close(c->fcc);
    if (c->ret) ret_client_close(c->ret);
    mcast_close(c->m);
    free(c->iface);
    free(c->ring);
    free(c);
    return dup;
  }
  c->next = g_open;
  g_open = c;
  rebuild_snapshot();
  pthread_mutex_unlock(&g_lock);
  return c;
}

void capture_close(capture_ctx_t *ctx) {
  if (!ctx) return;
  if (atomic_fetch_sub_explicit(&ctx->refcount, 1, memory_order_acq_rel) != 1) return;
  unlink_ctx(ctx);
  capture_wait_pumps_quiescent();
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

capture_ctx_t *capture_ref(capture_ctx_t *ctx) {
  atomic_fetch_add_explicit(&ctx->refcount, 1, memory_order_relaxed);
  return ctx;
}

_Atomic int *capture_ts_push_head_ptr(capture_ctx_t *ctx) { return &ctx->ts_push_head; }
_Atomic(void *) *capture_hls_seg_head_ptr(capture_ctx_t *ctx) { return &ctx->hls_seg_head; }
