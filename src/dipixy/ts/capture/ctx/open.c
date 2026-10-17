/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dipixy/ts/capture/priv.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "dipixy/version.h"

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
  capture_ctx_t *c;
  capture_ctx_t *dup;
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
