/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#include "priv.h"

int cidr_parse(const char *s, cidr_t *c) {
  char buf[64];
  char *slash;
  int prefix;
  strncpy(buf, s, sizeof buf - 1);
  buf[sizeof buf - 1] = '\0';
  slash = strchr(buf, '/');
  if (!slash)
    return -1;
  *slash = '\0';
  prefix = atoi(slash + 1);

  if (strchr(buf, ':')) {
    if (prefix < 0 || prefix > 128 || inet_pton(AF_INET6, buf, &c->u.v6.addr) != 1)
      return -1;
    c->family = AF_INET6;
    c->u.v6.prefix = (unsigned)prefix;
    return 0;
  }
  if (prefix < 0 || prefix > 32 || inet_pton(AF_INET, buf, &c->u.v4.addr) != 1)
    return -1;
  c->family = AF_INET;
  c->u.v4.mask.s_addr = prefix ? htonl(0xFFFFFFFFu << (32 - prefix)) : 0;
  return 0;
}

static int v6_prefix_match(const struct in6_addr *a, const struct in6_addr *b, unsigned prefix) {
  unsigned full_bytes = prefix / 8, rem_bits = prefix % 8;
  if (full_bytes && memcmp(a->s6_addr, b->s6_addr, full_bytes) != 0)
    return 0;
  if (rem_bits) {
    unsigned char mask = (unsigned char)(0xFF << (8 - rem_bits));
    if ((a->s6_addr[full_bytes] & mask) != (b->s6_addr[full_bytes] & mask))
      return 0;
  }
  return 1;
}

int in_ranges(int family, const void *addr, const cidr_t *ranges, size_t range_count) {
  size_t i;
  for (i = 0; i < range_count; i++) {
    const cidr_t *c = &ranges[i];
    if (c->family != family)
      continue;
    if (family == AF_INET) {
      const struct in_addr *a4 = (const struct in_addr *)addr;
      if ((a4->s_addr & c->u.v4.mask.s_addr) == (c->u.v4.addr.s_addr & c->u.v4.mask.s_addr))
        return 1;
    } else if (v6_prefix_match((const struct in6_addr *)addr, &c->u.v6.addr, c->u.v6.prefix)) {
      return 1;
    }
  }
  return 0;
}
