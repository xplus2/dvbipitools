/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "cas_scramble_engine.h"

struct cas_scramble_engine {
  scrambler_t *scr;
  unsigned flush_pid;

  cas_pid_state_t pid[CAS_SCRAMBLE_ENGINE_MAX_PIDS];
  size_t pid_count;
  size_t last_pid_idx; /* MRU 1-entry cache: consecutive packets are usually the same pid */

  size_t cw_cache_len[2]; /* 0: no key material published for this parity */

  unsigned long long scrambled_packets_total;
  unsigned long long unexpected_clear_packets_total;
};

void cas_pid_apply(cas_pid_state_t *ps, int have_target, int target_parity, int pusi, double now, double force_flip_s) {
  if (!have_target)
    return;
  if (target_parity != ps->current_parity && !ps->flip_pending) {
    ps->flip_pending = 1;
    ps->flip_deadline_wall = now + force_flip_s;
  }
  if (ps->flip_pending && (pusi || now >= ps->flip_deadline_wall)) {
    ps->current_parity = target_parity;
    ps->flip_pending = 0;
  }
}

static cas_pid_state_t *find_pid_state(cas_scramble_engine_t *e, unsigned pid) {
  if (e->last_pid_idx < e->pid_count && e->pid[e->last_pid_idx].pid == pid)
    return &e->pid[e->last_pid_idx];

  for (size_t i = 0; i < e->pid_count; i++)
    if (e->pid[i].pid == pid) {
      e->last_pid_idx = i;
      return &e->pid[i];
    }
  return NULL;
}

static void queue_clear(cas_scramble_engine_t *e, unsigned out_pid, unsigned char pkt188[188], scrambler_emit_cb emit, void *ctx) {
  scrambler_passthrough_queued(e->scr, pkt188, emit, ctx);
  if (out_pid == e->flush_pid)
    scrambler_flush(e->scr, emit, ctx);
}

cas_scramble_engine_t *cas_scramble_engine_start(scramble_algo_t algo, const unsigned *pids, size_t pid_count, unsigned flush_pid) {
  cas_scramble_engine_t *e;
  size_t n;

  e = calloc(1, sizeof *e);
  if (!e) return NULL;
  e->scr = scrambler_new(algo);
  if (!e->scr) {
    free(e);
    return NULL;
  }
  n = pid_count < CAS_SCRAMBLE_ENGINE_MAX_PIDS ? pid_count : CAS_SCRAMBLE_ENGINE_MAX_PIDS;
  for (size_t i = 0; i < n; i++)
    e->pid[i].pid = pids[i];
  e->pid_count = n;
  e->flush_pid = flush_pid;
  return e;
}

int cas_scramble_engine_add_pid(cas_scramble_engine_t *e, unsigned pid) {
  if (find_pid_state(e, pid)) return 0;
  if (e->pid_count >= CAS_SCRAMBLE_ENGINE_MAX_PIDS) return -1;
  memset(&e->pid[e->pid_count], 0, sizeof e->pid[0]);
  e->pid[e->pid_count++].pid = pid;
  return 0;
}

void cas_scramble_engine_stop(cas_scramble_engine_t *e) {
  if (!e) return;
  scrambler_free(e->scr);
  free(e);
}

void cas_scramble_engine_set_cw(cas_scramble_engine_t *e, int parity, const unsigned char *cw, size_t len, scrambler_emit_cb emit, void *ctx) {
  if (parity != SCRAMBLE_PARITY_EVEN && parity != SCRAMBLE_PARITY_ODD) return;
  e->cw_cache_len[parity] = len;
  if (len)
    scrambler_set_key(e->scr, parity, cw, len, emit, ctx);
}

void cas_scramble_engine_scramble_packet(cas_scramble_engine_t *e, unsigned out_pid, int have_source, int have_target, int target_parity, int cw_valid, double now, unsigned char pkt188[188], scrambler_emit_cb emit, void *ctx) {
  cas_pid_state_t *ps = find_pid_state(e, out_pid);
  int pusi;
  if (!ps) {
    queue_clear(e, out_pid, pkt188, emit, ctx);
    return;
  }
  if (!have_source) {
    e->unexpected_clear_packets_total++; /* managed pid, source not up */
    queue_clear(e, out_pid, pkt188, emit, ctx);
    return;
  }
  pusi = (pkt188[1] & 0x40) != 0;
  cas_pid_apply(ps, have_target, target_parity, pusi, now, CAS_FORCE_FLIP_S);

  if (!e->cw_cache_len[ps->current_parity] || !cw_valid) {
    e->unexpected_clear_packets_total++;
    queue_clear(e, out_pid, pkt188, emit, ctx);
    return;
  }
  e->scrambled_packets_total++;
  scrambler_encrypt_packet_queued(e->scr, pkt188, ps->current_parity, emit, ctx);
  if (out_pid == e->flush_pid) scrambler_flush(e->scr, emit, ctx); /* never let batching delay the caller's clock pid */
}

void cas_scramble_engine_flush(cas_scramble_engine_t *e, scrambler_emit_cb emit, void *ctx) {
  if (e) scrambler_flush(e->scr, emit, ctx);
}

void cas_scramble_engine_get_metrics(cas_scramble_engine_t *e, unsigned long long *scrambled_packets_total, unsigned long long *unexpected_clear_packets_total) {
  *scrambled_packets_total = e ? e->scrambled_packets_total : 0;
  *unexpected_clear_packets_total = e ? e->unexpected_clear_packets_total : 0;
}
