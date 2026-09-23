/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP3

#include "../http3.h"
#include "../http3_int.h"

#include "lib/helper/ioutil.h"

int g_h3_max_reqs = H3_DEFAULT_MAX_REQS;
uint64_t g_h3_idle_ns = H3_DEFAULT_IDLE_S * 1000000000ULL;
static int g_h3_conns_cap = H3_DEFAULT_MAX_CONNS;
int g_h3_max_conns = H3_DEFAULT_MAX_CONNS;
size_t g_h3_max_udp = H3_DEFAULT_MAX_UDP;
uint64_t g_h3_window = H3_DEFAULT_WINDOW_KIB * 1024;
ngtcp2_cc_algo g_h3_cc = NGTCP2_CC_ALGO_CUBIC;
uint16_t g_h3_probes[2];
size_t g_h3_nprobes;
uint32_t g_h3_hash_cap = H3_DEFAULT_MAX_CONNS * H3_HASH_PER_CONN;

void h3_set_limits(unsigned max_streams, unsigned max_conns, unsigned idle_s) {
  if (!max_streams) max_streams = H3_DEFAULT_MAX_REQS;
  if (max_streams < H3_MIN_MAX_REQS) max_streams = H3_MIN_MAX_REQS;
  if (max_streams > H3_MAX_MAX_REQS) max_streams = H3_MAX_MAX_REQS;
  g_h3_max_reqs = (int)max_streams;
  g_h3_conns_cap = max_conns ? (int)max_conns : H3_DEFAULT_MAX_CONNS;
  g_h3_idle_ns = (uint64_t)(idle_s ? idle_s : H3_DEFAULT_IDLE_S) * 1000000000ULL;
}

void h3_set_transport(unsigned max_udp, unsigned window_kib, int cc) {
  if (!max_udp) max_udp = H3_DEFAULT_MAX_UDP;
  if (max_udp < NGTCP2_MAX_UDP_PAYLOAD_SIZE) max_udp = NGTCP2_MAX_UDP_PAYLOAD_SIZE;
  if (max_udp > H3_UDP_MAX_PAYLOAD) max_udp = H3_UDP_MAX_PAYLOAD;
  g_h3_max_udp = max_udp;
  g_h3_nprobes = 0;
  if (max_udp > H3_DEFAULT_MAX_UDP) g_h3_probes[g_h3_nprobes++] = H3_DEFAULT_MAX_UDP;
  if (max_udp > NGTCP2_MAX_UDP_PAYLOAD_SIZE) g_h3_probes[g_h3_nprobes++] = (uint16_t)max_udp;
  g_h3_window = (window_kib ? window_kib : H3_DEFAULT_WINDOW_KIB) * 1024ULL;
  g_h3_cc = NGTCP2_CC_ALGO_CUBIC;
  if (cc == 1) g_h3_cc = NGTCP2_CC_ALGO_BBR;
  else if (cc == 2) g_h3_cc = NGTCP2_CC_ALGO_RENO;
}

void h3_set_max_conns_per_thread(int n) {
  if (n < 1) n = 1;
  if (n > g_h3_conns_cap) n = g_h3_conns_cap;
  g_h3_max_conns = n;
  g_h3_hash_cap = (uint32_t)next_pow2((size_t)n * H3_HASH_PER_CONN);
}

#endif /* HAVE_HTTP3 */
