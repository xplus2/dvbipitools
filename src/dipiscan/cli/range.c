/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"

#include "priv.h"

/* full multicast address, family from ':' presence */
static int base_parse(const char *s, int *family, unsigned char *base) {
  if (strchr(s, ':')) {
    struct in6_addr a6;
    if (inet_pton(AF_INET6, s, &a6) != 1) return -1;
    if (a6.s6_addr[0] != 0xFF) return -1;
    memcpy(base, &a6, 16);
    *family = AF_INET6;
  } else {
    struct in_addr a;
    if (inet_pton(AF_INET, s, &a) != 1) return -1;
    if ((ntohl(a.s_addr) >> 28) != 0xE) return -1; /* 224.0.0.0/4 */
    memcpy(base, &a, 4);
    *family = AF_INET;
  }
  return 0;
}

void args_range_describe(const config_t *cfg, char *buf, size_t n) {
  char lo[64];
  char hi[64];
  int af = cfg->family == AF_INET6 ? AF_INET6 : AF_INET;
  inet_ntop(af, cfg->start, lo, sizeof lo);
  inet_ntop(af, cfg->end, hi, sizeof hi);
  snprintf(buf, n, "%s-%s", lo, hi);
}

/* cap on swept addresses: do not sweep millions of candidates */
#define MAX_SWEEP_HOSTBITS 20
#define MAX_SWEEP_ADDRS (1u << MAX_SWEEP_HOSTBITS)

/* end-start, capped. -1 if end<start or range exceeds cap */
static int addr_diff_capped(const unsigned char *start, const unsigned char *end, int alen, unsigned cap, unsigned *out) {
  unsigned char diff[16] = {0};
  int borrow = 0;
  unsigned val;
  for (int i = alen - 1; i >= 0; i--) {
    int d = (int)end[i] - (int)start[i] - borrow;
    if (d < 0) {
      d += 256;
      borrow = 1;
    } else {
      borrow = 0;
    }
    diff[i] = (unsigned char)d;
  }
  if (borrow) return -1;
  for (int i = 0; i < alen - 4; i++) {
    if (diff[i]) return -1;
  }
  val = 0;
  for (int i = alen >= 4 ? alen - 4 : 0; i < alen; i++) val = (val << 8) | diff[i];
  if (val > cap) return -1;
  *out = val;
  return 0;
}

/* addr/prefixlen. whole block, all addresses are valid groups */
static int cidr_parse(const char *addrs, const char *prefixs, int *family, unsigned char *start, unsigned char *end, unsigned *total) {
  unsigned char addr[16];
  unsigned char net[16];
  unsigned char top[16];
  int fam;
  int alen;
  int maxprefix;
  int hostbits;
  int bit;
  int i;
  char *pend;
  long prefix;
  if (base_parse(addrs, &fam, addr)) return -1;
  errno = 0;
  prefix = strtol(prefixs, &pend, 10);
  if (errno || pend == prefixs || *pend != '\0' || prefix < 0) return -1;
  alen = (fam == AF_INET6) ? 16 : 4;
  maxprefix = alen * 8;
  if (prefix > maxprefix) return -1;
  hostbits = maxprefix - (int)prefix;
  if (hostbits > MAX_SWEEP_HOSTBITS) return -1;
  memcpy(net, addr, (size_t)alen);
  memcpy(top, addr, (size_t)alen);
  bit = 0;
  i = alen - 1;
  while (bit < hostbits) {
    int bits_here = hostbits - bit < 8 ? hostbits - bit : 8;
    unsigned char mask = (unsigned char)((1u << bits_here) - 1);
    net[i] &= (unsigned char)~mask;
    top[i] |= mask;
    bit += bits_here;
    i--;
  }

  memset(start, 0, 16);
  memset(end, 0, 16);
  memcpy(start, net, (size_t)alen);
  memcpy(end, top, (size_t)alen);
  *family = fam;
  *total = 1u << hostbits;
  return 0;
}

/* startaddr-stopaddr (incl.) */
static int range_parse(const char *los, const char *his, int *family, unsigned char *start, unsigned char *end, unsigned *total) {
  int fam_lo;
  int fam_hi;
  int alen;
  unsigned char lo[16];
  unsigned char hi[16];
  unsigned diff;
  if (base_parse(los, &fam_lo, lo) || base_parse(his, &fam_hi, hi)) return -1;
  if (fam_lo != fam_hi) return -1;
  alen = (fam_lo == AF_INET6) ? 16 : 4;
  if (memcmp(lo, hi, (size_t)alen) > 0) return -1;
  if (addr_diff_capped(lo, hi, alen, MAX_SWEEP_ADDRS - 1u, &diff)) return -1;
  *family = fam_lo;
  memcpy(start, lo, 16);
  memcpy(end, hi, 16);
  *total = diff + 1u;
  return 0;
}

/* default /24, last byte swept 0..255 */
static void plain_parse(const unsigned char *addr, int family, unsigned char *start, unsigned char *end, unsigned *total) {
  int alen = (family == AF_INET6) ? 16 : 4;
  memcpy(start, addr, 16);
  memcpy(end, addr, 16);
  start[alen - 1] = 0;
  end[alen - 1] = 255;
  *total = 256;
}

/* plain addr, CIDR or startaddr-stopaddr */
int scan_mcast_range_parse(const char *s, int *family, unsigned char *start, unsigned char *end, unsigned *total) {
  const char *slash = strchr(s, '/');
  const char *dash = strchr(s, '-');
  unsigned char addr[16];
  if (slash) {
    char addrbuf[64];
    size_t len = (size_t)(slash - s);
    if (len == 0 || len >= sizeof addrbuf) return -1;
    memcpy(addrbuf, s, len);
    addrbuf[len] = '\0';
    return cidr_parse(addrbuf, slash + 1, family, start, end, total);
  }
  if (dash) {
    char lobuf[64];
    size_t len = (size_t)(dash - s);
    if (len == 0 || len >= sizeof lobuf) return -1;
    memcpy(lobuf, s, len);
    lobuf[len] = '\0';
    return range_parse(lobuf, dash + 1, family, start, end, total);
  }
  if (base_parse(s, family, addr)) return -1;
  plain_parse(addr, *family, start, end, total);
  return 0;
}

/* port or port-port, inclusive range */
int scan_port_range_parse(const char *s, unsigned *lo, unsigned *hi) {
  const char *dash = strchr(s, '-');
  char buf[16];
  size_t len;
  if (!dash) {
    if (argutil_port_parse(s, lo)) return -1;
    *hi = *lo;
    return 0;
  }
  len = (size_t)(dash - s);
  if (len == 0 || len >= sizeof buf) return -1;
  memcpy(buf, s, len);
  buf[len] = '\0';
  if (argutil_port_parse(buf, lo)) return -1;
  if (argutil_port_parse(dash + 1, hi)) return -1;
  if (*lo > *hi) return -1;
  return 0;
}

int scan_cfg_mcast(config_t *cfg, const char *s) {
  return scan_mcast_range_parse(s, &cfg->family, cfg->start, cfg->end, &cfg->total);
}

int scan_cfg_port(config_t *cfg, const char *s) {
  return scan_port_range_parse(s, &cfg->port_lo, &cfg->port_hi);
}

