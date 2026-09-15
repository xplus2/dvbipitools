/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_SEGSTORE_PRIV_H
#define DIPIXY_SEGSTORE_PRIV_H

#include "../segstore_int.h"

enum { STORE_FREE = 0, STORE_OPENING = 1, STORE_OPEN = 2, STORE_CLOSING = 3 };

typedef struct slot_retire_node {
  int idx;
  hls_snapshot_t *snaps[HLS_SNAP_RETIRE_DEPTH + 1];
  int nsnaps;
  uint64_t *mark;
  _Atomic(struct slot_retire_node *) next;
} slot_retire_node_t;

extern qsbr_domain_t *g_segstore_qsbr;
extern _Atomic int *g_slot_state;

int seg_pool_class_for(size_t size);
size_t seg_pool_class_cap(int cls);

hls_snapshot_t *snap_clone(const hls_snapshot_t *base);
void snap_free(hls_snapshot_t *ns);

void snap_retire(hls_store_t *s, hls_snapshot_t *old);
void snap_retire_async(hls_snapshot_t *snap);
void snap_drain_all_async(hls_store_t *s);
void slot_retire_push(slot_retire_node_t *node);

#endif
