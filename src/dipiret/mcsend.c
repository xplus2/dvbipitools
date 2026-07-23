/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdatomic.h>
#include <stdlib.h>

#include "mcsend.h"

struct mcsend_entry {
  _Atomic(channel_t *) key;
  mcast_t *sock;
};

struct mcsend_table {
  struct mcsend_entry *entries;
  size_t max_channels;
  const char *iface;
  int ttl;
};

mcsend_table_t *mcsend_table_new(size_t max_channels, const char *iface, int ttl) {
  mcsend_table_t *t = calloc(1, sizeof *t);
  if (!t)
    return NULL;
  t->entries = calloc(max_channels, sizeof *t->entries);
  if (!t->entries) {
    free(t);
    return NULL;
  }
  t->max_channels = max_channels;
  t->iface = iface;
  t->ttl = ttl;
  return t;
}

void mcsend_table_free(mcsend_table_t *t) {
  size_t i;
  if (!t)
    return;
  for (i = 0; i < t->max_channels; i++) {
    if (t->entries[i].sock)
      mcast_close(t->entries[i].sock);
  }
  free(t->entries);
  free(t);
}

void mcsend_ensure(mcsend_table_t *t, channel_t *c, unsigned ff_port) {
  size_t i;
  unsigned port;
  mcast_t *m;

  for (i = 0; i < t->max_channels; i++) {
    if (atomic_load_explicit(&t->entries[i].key, memory_order_acquire) == c)
      return; /* already have a socket for this channel */
  }

  port = ff_port ? ff_port : c->port;
  m = mcast_open_send(c->family, c->group, port, t->iface, t->ttl);
  if (!m)
    return; /* mcast_open_send already logged the reason */

  for (i = 0; i < t->max_channels; i++) {
    if (atomic_load_explicit(&t->entries[i].key, memory_order_relaxed) == NULL) {
      t->entries[i].sock = m; /* plain write: single writer, not yet published */
      atomic_store_explicit(&t->entries[i].key, c, memory_order_release);
      return;
    }
  }
  mcast_close(m); /* table full - shouldn't happen when sized to match channel_table_t */
}

mcast_t *mcsend_get(mcsend_table_t *t, channel_t *c) {
  size_t i;
  for (i = 0; i < t->max_channels; i++) {
    if (atomic_load_explicit(&t->entries[i].key, memory_order_acquire) == c)
      return t->entries[i].sock;
  }
  return NULL;
}
