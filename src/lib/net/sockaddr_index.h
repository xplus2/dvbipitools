/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_NET_SOCKADDR_INDEX_H
#define LIB_NET_SOCKADDR_INDEX_H

#include <stddef.h>
#include <sys/socket.h>

/* addr->slot open-addressed hash index.
   no payload: caller keeps own slot array. O(1) avg lookup vs linear scan. not thread-safe, caller serializes. */
typedef struct sockaddr_index sockaddr_index_t;

/* cap: max live keys, same as caller's slot pool. table sized for <75% load */
sockaddr_index_t *sockaddr_index_new(size_t cap);
void sockaddr_index_free(sockaddr_index_t *idx);

/* slot for addr, SIZE_MAX if none. addr NULL / addrlen 0 collapse onto one shared key */
size_t sockaddr_index_find(const sockaddr_index_t *idx, const struct sockaddr *addr, socklen_t addrlen);

/* associates addr with slot_idx. caller must sockaddr_index_find() first: undefined behavior if addr already present */
void sockaddr_index_insert(sockaddr_index_t *idx, const struct sockaddr *addr, socklen_t addrlen, size_t slot_idx);

/* removes addr's entry if present, no-op otherwise */
void sockaddr_index_remove(sockaddr_index_t *idx, const struct sockaddr *addr, socklen_t addrlen);

/* same key canonicalization/hash as this index, for callers sharding a table by client
   address (lock striping). stripe_count must be > 0. NULL/0-length addr always maps to
   stripe 0, matching this index's own NULL/0 -> shared-key collapse */
size_t sockaddr_stripe_of(const struct sockaddr *addr, socklen_t addrlen, size_t stripe_count);

#endif
