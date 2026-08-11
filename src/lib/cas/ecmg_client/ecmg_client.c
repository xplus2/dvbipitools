/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "lib/log.h"
#include "lib/signal.h"

#include "priv.h"

int ecmg_stopping(const ecmg_client_t *c) {
  return atomic_load_explicit(&c->stop, memory_order_relaxed) || signal_stop_requested();
}

int ecmg_client_get_cw(ecmg_client_t *c, int slot, unsigned char *cw_out, size_t cw_cap, size_t *cw_len_out) {
  int have;
  if (slot != 0 && slot != 1)
    return -1;
  pthread_mutex_lock(&c->cw_lock);
  have = c->cw_slot_have[slot];
  if (have) {
    if (cw_cap < c->cw_len) {
      pthread_mutex_unlock(&c->cw_lock);
      return -1;
    }
    memcpy(cw_out, c->cw_slot[slot], c->cw_len);
    *cw_len_out = c->cw_len;
  }
  pthread_mutex_unlock(&c->cw_lock);
  return have ? 0 : -1;
}

unsigned long ecmg_client_cw_epoch(ecmg_client_t *c) {
  return atomic_load_explicit(&c->cw_epoch, memory_order_relaxed);
}

int ecmg_ecm_available_calc(ecmg_outage_mode_t outage_mode, int connected) {
  return !(outage_mode == ECMG_OUTAGE_SILENT && !connected);
}

int ecmg_client_get_ecm(ecmg_client_t *c, unsigned char *out, size_t cap, size_t *len_out) {
  int have;
  if (!ecmg_ecm_available_calc(c->cfg.outage_mode, atomic_load_explicit(&c->connected, memory_order_relaxed)))
    return -1;
  pthread_mutex_lock(&c->ecm_lock);
  have = c->ecm_len > 0;
  if (have) {
    if (cap < c->ecm_len) {
      pthread_mutex_unlock(&c->ecm_lock);
      return -1;
    }
    memcpy(out, c->ecm, c->ecm_len);
    *len_out = c->ecm_len;
  }
  pthread_mutex_unlock(&c->ecm_lock);
  return have ? 0 : -1;
}

unsigned long ecmg_client_ecm_epoch(ecmg_client_t *c) {
  return atomic_load_explicit(&c->ecm_epoch, memory_order_relaxed);
}

unsigned ecmg_client_ecm_rep_period_ms(ecmg_client_t *c) {
  return atomic_load_explicit(&c->ecm_rep_period_ms, memory_order_relaxed);
}

int ecmg_target_parity_calc(ecmg_outage_mode_t outage_mode, int connected, unsigned long packets_per_cp, unsigned long cur, unsigned long published_at, unsigned long epoch) {
  unsigned long periods_elapsed;
  if (outage_mode != ECMG_OUTAGE_CYCLING)
    return (int)(epoch & 1UL);
  if (connected)
    return (int)(epoch & 1UL);
  if (packets_per_cp == 0)
    return (int)(epoch & 1UL);
  periods_elapsed = (cur - published_at) / packets_per_cp;
  return (int)((epoch + periods_elapsed) & 1UL);
}

int ecmg_client_connected(ecmg_client_t *c) { return atomic_load_explicit(&c->connected, memory_order_relaxed); }
unsigned long ecmg_client_cryptoperiod_transitions(ecmg_client_t *c) { return atomic_load_explicit(&c->cryptoperiod_transitions_total, memory_order_relaxed); }
unsigned long ecmg_client_ecm_total(ecmg_client_t *c) { return atomic_load_explicit(&c->ecm_total, memory_order_relaxed); }
unsigned long ecmg_client_ecm_errors(ecmg_client_t *c) { return atomic_load_explicit(&c->ecm_errors_total, memory_order_relaxed); }

/* frozen: always the last published parity. cycling, while disconnected: keeps alternating
   on the normal crypto-period schedule between the two last-known CWs, computed from how many
   whole periods have elapsed since the last publish. no waiting on the ECMG to come back.
   uses ecm_epoch, not cw_epoch: cw_epoch bumps the instant a CW_provision is sent, before the
   ECMG round trip; switching live scrambling on that would flip parity before the matching ECM
   is even on the wire. ecm_epoch only bumps once the ECM_response actually arrived. */
int ecmg_client_target_parity(ecmg_client_t *c) {
  unsigned long epoch = atomic_load_explicit(&c->ecm_epoch, memory_order_relaxed);
  unsigned long cur = atomic_load_explicit(c->packet_counter, memory_order_relaxed);
  unsigned long published_at = atomic_load_explicit(&c->cw_published_at, memory_order_relaxed);
  int connected = atomic_load_explicit(&c->connected, memory_order_relaxed);
  return ecmg_target_parity_calc(c->cfg.outage_mode, connected, c->packets_per_cp, cur, published_at, epoch);
}

ecmg_client_t *ecmg_client_start(const ecmg_client_cfg_t *cfg, const atomic_ulong *packet_counter, unsigned long packets_per_cp, unsigned long lookahead_margin_packets) {
  ecmg_client_t *c = calloc(1, sizeof *c);
  if (!c)
    return NULL;
  c->cfg = *cfg;
  c->cw_len = scrambler_cw_len(cfg->algo);
  c->packet_counter = packet_counter;
  c->packets_per_cp = packets_per_cp;
  c->lookahead_margin_packets = lookahead_margin_packets;
  pthread_mutex_init(&c->cw_lock, NULL);
  pthread_mutex_init(&c->ecm_lock, NULL);
  if (pthread_create(&c->thread, NULL, ecmg_client_main, c) != 0) {
    log_line("ecmg: pthread_create: %s", strerror(errno));
    pthread_mutex_destroy(&c->cw_lock);
    pthread_mutex_destroy(&c->ecm_lock);
    free(c);
    return NULL;
  }
  return c;
}

void ecmg_client_stop(ecmg_client_t *c) {
  if (!c)
    return;
  atomic_store_explicit(&c->stop, 1, memory_order_relaxed);
  pthread_join(c->thread, NULL);
  pthread_mutex_destroy(&c->cw_lock);
  pthread_mutex_destroy(&c->ecm_lock);
  free(c);
}
