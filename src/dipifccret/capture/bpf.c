/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <linux/if_ether.h>

#include "capture.h"

#define CAPTURE_BPF_MAX_INSNS 4096u /* kernel classic-BPF program length limit */

/* emit_v4_clause() instruction count: LD, AND, JEQ, RET */
#define BPF_V4_CLAUSE_INSNS 4u
/* emit_v6_clause()'s per-word instruction count: LD, AND, JEQ. its own jf offsets and BPF_V6_CLAUSE_INSNS key off this */
#define BPF_V6_WORD_INSNS 3u
/* emit_v6_clause()'s total: 4 words + trailing RET. bpf_dispatch_len() keys off this */
#define BPF_V6_CLAUSE_INSNS (4u * BPF_V6_WORD_INSNS + 1u)

typedef struct {
  struct sock_filter *insns;
  size_t len, cap;
} bpf_buf_t;

static int bpf_emit(bpf_buf_t *b, struct sock_filter insn) {
  if (b->len == b->cap) {
    size_t new_cap = b->cap ? b->cap * 2 : 64;
    struct sock_filter *grown = realloc(b->insns, new_cap * sizeof *grown);
    if (!grown)
      return -1;
    b->insns = grown;
    b->cap = new_cap;
  }
  b->insns[b->len++] = insn;
  return 0;
}

/* match: dst v4 addr at addr_off, masked. match falls through to RET+accept below, mismatch (jf=1) skips to next clause */
static int emit_v4_clause(bpf_buf_t *b, unsigned addr_off, const cidr_t *c) {
  uint32_t mask = ntohl(c->u.v4.mask.s_addr);
  uint32_t net = ntohl(c->u.v4.addr.s_addr) & mask;
  if (bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, addr_off)) < 0)
    return -1;
  if (bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_ALU | BPF_AND | BPF_K, mask)) < 0)
    return -1;
  if (bpf_emit(b, (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, net, 0, 1)) < 0)
    return -1;
  return bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, 0xFFFFFFFFu));
}

/* prefix-derived mask for 32-bit word w (0-based) of a v6 address */
static uint32_t v6_word_mask(unsigned prefix, unsigned w) {
  unsigned word_bits = w * 32;
  if (prefix <= word_bits)
    return 0;
  if (prefix >= word_bits + 32)
    return 0xFFFFFFFFu;
  return 0xFFFFFFFFu << (word_bits + 32 - prefix);
}

/* 4 word-checks chained by fixed jf offsets (10/7/4/1), self-contained regardless of range count, no cross-clause backpatching */
static int emit_v6_clause(bpf_buf_t *b, unsigned addr_off, const cidr_t *c) {
  for (unsigned w = 0; w < 4; w++) {
    uint32_t raw, mask, net;
    unsigned jf;
    memcpy(&raw, &c->u.v6.addr.s6_addr[w * 4], 4);
    mask = v6_word_mask(c->u.v6.prefix, w);
    net = ntohl(raw) & mask;
    jf = (w == 3) ? 1 : (3 - w) * BPF_V6_WORD_INSNS + 1;
    if (bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, addr_off + w * 4)) < 0)
      return -1;
    if (bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_ALU | BPF_AND | BPF_K, mask)) < 0)
      return -1;
    if (bpf_emit(b, (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, net, 0, jf)) < 0)
      return -1;
  }
  return bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, 0xFFFFFFFFu));
}

static size_t bpf_dispatch_len(size_t nv4, size_t nv6) {
  return BPF_V4_CLAUSE_INSNS * nv4 + BPF_V6_CLAUSE_INSNS * nv6 + 5;
}

/* assumes A = ethertype on entry. base: ethernet-payload offset (14 no vlan, 18 vlan tag unwrapped) */
static int emit_dispatch_block(bpf_buf_t *b, unsigned base, const cidr_t *ranges, size_t range_count) {
  size_t nv4 = 0, nv6 = 0;
  unsigned v4_section_len;

  for (size_t i = 0; i < range_count; i++)
    if (ranges[i].family == AF_INET)
      nv4++;
    else
      nv6++;
  v4_section_len = (unsigned)(BPF_V4_CLAUSE_INSNS * nv4 + 1);

  if (bpf_emit(b, (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETH_P_IP, 0, v4_section_len)) < 0)
    return -1;
  for (size_t i = 0; i < range_count; i++)
    if (ranges[i].family == AF_INET && emit_v4_clause(b, base + 16, &ranges[i]) < 0)
      return -1;
  if (bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, 0)) < 0) /* v4, no range matched */
    return -1;

  if (bpf_emit(b, (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETH_P_IPV6, 1, 0)) < 0)
    return -1;
  if (bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, 0)) < 0) /* neither v4 nor v6 */
    return -1;

  for (size_t i = 0; i < range_count; i++)
    if (ranges[i].family == AF_INET6 && emit_v6_clause(b, base + 24, &ranges[i]) < 0)
      return -1;
  return bpf_emit(b, (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, 0)); /* v6, no range matched */
}

/* ethertype+vlan prologue, then 2 dispatch-block copies (base 14 / base 18). classic BPF lacks
   subroutines: with-vlan/without-vlan tail duplicated.   base-14 block can exceed 255 instructions
   (jt/jf are 8-bit): skipping it toward vlan path uses unconditional BPF_JA trampoline, no condjump. */
struct sock_filter *capture_build_bpf(const cidr_t *ranges, size_t range_count, size_t *out_len) {
  bpf_buf_t b;
  size_t nv4 = 0, nv6 = 0, d_len, total;

  memset(&b, 0, sizeof b);
  for (size_t i = 0; i < range_count; i++)
    if (ranges[i].family == AF_INET)
      nv4++;
    else
      nv6++;
  d_len = bpf_dispatch_len(nv4, nv6);
  total = 2 * d_len + 4;
  if (total > CAPTURE_BPF_MAX_INSNS)
    return NULL;

  if (bpf_emit(&b, (struct sock_filter)BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 12)) < 0)
    goto fail;
  if (bpf_emit(&b, (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETH_P_8021Q, 0, 1)) < 0)
    goto fail;
  if (bpf_emit(&b, (struct sock_filter)BPF_STMT(BPF_JMP | BPF_JA, (uint32_t)d_len)) < 0)
    goto fail;
  if (emit_dispatch_block(&b, 14, ranges, range_count) < 0)
    goto fail;
  if (bpf_emit(&b, (struct sock_filter)BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 16)) < 0)
    goto fail;
  if (emit_dispatch_block(&b, 18, ranges, range_count) < 0)
    goto fail;

  if (b.len != total) /* catch emit_v4_clause/emit_v6_clause drifting from BPF_V4/V6_*_INSNS */
    goto fail;

  *out_len = b.len;
  return b.insns;
fail:
  free(b.insns);
  return NULL;
}
