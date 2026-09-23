/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP3

#include "../http3.h"
#include "../http3_int.h"

#include <stdlib.h>
#include <string.h>

_Thread_local h3_conn_t **t_h3_active = NULL;
_Thread_local int t_h3_active_cnt = 0;
_Thread_local h3_hent_t *t_h3_hash = NULL;
_Thread_local int t_h3_init = 0;

_Thread_local h3_conn_t *t_h3_pool = NULL;
_Thread_local int *t_h3_pool_free = NULL;
_Thread_local int t_h3_pool_free_n = 0;
_Thread_local uint32_t t_h3_hash_cap = 0;

#define H3_HASH_TOMB ((h3_conn_t *)(uintptr_t)1)

int h3_tables_alloc(void) {
  if (t_h3_init)
    return 1;
  t_h3_pool = calloc((size_t)g_h3_max_conns, sizeof *t_h3_pool);
  t_h3_pool_free = malloc(sizeof *t_h3_pool_free * (size_t)g_h3_max_conns);
  t_h3_active = malloc(sizeof *t_h3_active * (size_t)g_h3_max_conns);
  t_h3_hash = calloc((size_t)g_h3_hash_cap, sizeof *t_h3_hash);
  if (!t_h3_pool || !t_h3_pool_free || !t_h3_active || !t_h3_hash) {
    free(t_h3_pool);
    free(t_h3_pool_free);
    free(t_h3_active);
    free(t_h3_hash);
    t_h3_pool = NULL;
    t_h3_pool_free = NULL;
    t_h3_active = NULL;
    t_h3_hash = NULL;
    return 0;
  }
  if (h3_udp_init(g_h3_max_udp) != 0) {
    free(t_h3_pool);
    free(t_h3_pool_free);
    free(t_h3_active);
    free(t_h3_hash);
    t_h3_pool = NULL;
    t_h3_pool_free = NULL;
    t_h3_active = NULL;
    t_h3_hash = NULL;
    return 0;
  }
  for (int i = 0; i < g_h3_max_conns; i++)
    t_h3_pool_free[i] = i;
  t_h3_pool_free_n = g_h3_max_conns;
  t_h3_hash_cap = g_h3_hash_cap;
  t_h3_init = 1;
  return 1;
}

void h3_tables_free(void) {
  free(t_h3_pool);
  free(t_h3_pool_free);
  free(t_h3_active);
  free(t_h3_hash);
  t_h3_pool = NULL;
  t_h3_pool_free = NULL;
  t_h3_active = NULL;
  t_h3_hash = NULL;
  t_h3_pool_free_n = 0;
  t_h3_active_cnt = 0;
  t_h3_init = 0;
}

static uint32_t cid_hash(const uint8_t *data, size_t len) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; i++) h = (h ^ data[i]) * 16777619u;
  return h;
}

int h3_hash_insert(const ngtcp2_cid *cid, h3_conn_t *c) {
  uint32_t i = cid_hash(cid->data, cid->datalen) & (t_h3_hash_cap - 1);
  for (uint32_t n = 0; n < t_h3_hash_cap; n++, i = (i + 1) & (t_h3_hash_cap - 1)) {
    if (t_h3_hash[i].c == NULL || t_h3_hash[i].c == H3_HASH_TOMB) {
      t_h3_hash[i].c = c;
      t_h3_hash[i].cid = *cid;
      return 0;
    }
  }
  return -1;
}

/* tombstones only, cid.c slot */
void h3_hash_delete(const ngtcp2_cid *cid, const h3_conn_t *c) {
  uint32_t i = cid_hash(cid->data, cid->datalen) & (t_h3_hash_cap - 1);
  for (uint32_t n = 0; n < t_h3_hash_cap; n++, i = (i + 1) & (t_h3_hash_cap - 1)) {
    if (t_h3_hash[i].c == NULL) return;
    if (t_h3_hash[i].c == c && ngtcp2_cid_eq(&t_h3_hash[i].cid, cid)) {
      t_h3_hash[i].c = H3_HASH_TOMB;
      return;
    }
  }
}

int h3_cid_add(h3_conn_t *c, const ngtcp2_cid *cid) {
  if (c->ncids >= H3_MAX_CIDS || h3_hash_insert(cid, c) != 0) return -1;
  c->cids[c->ncids++] = *cid;
  return 0;
}

void h3_cid_remove(h3_conn_t *c, const ngtcp2_cid *cid) {
  for (int i = 0; i < c->ncids; i++) {
    if (!ngtcp2_cid_eq(&c->cids[i], cid)) continue;
    c->cids[i] = c->cids[--c->ncids];
    h3_hash_delete(cid, c);
    return;
  }
}

h3_conn_t *find_conn(const uint8_t *pkt, size_t pktlen) {
  if (!t_h3_init) return NULL;
  ngtcp2_version_cid vc;
  if (ngtcp2_pkt_decode_version_cid(&vc, pkt, pktlen, H3_SCID_LEN) != 0) return NULL;
  uint32_t i = cid_hash(vc.dcid, vc.dcidlen) & (t_h3_hash_cap - 1);
  for (uint32_t n = 0; n < t_h3_hash_cap; n++, i = (i + 1) & (t_h3_hash_cap - 1)) {
    const h3_hent_t *e = &t_h3_hash[i];
    if (!e->c) return NULL;
    if (e->c == H3_HASH_TOMB || e->c->done) continue;
    if (e->cid.datalen == vc.dcidlen && memcmp(e->cid.data, vc.dcid, vc.dcidlen) == 0) return e->c;
  }
  return NULL;
}

#endif /* HAVE_HTTP3 */
