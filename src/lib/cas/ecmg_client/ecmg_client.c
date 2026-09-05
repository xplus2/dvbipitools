/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/log.h"
#include "lib/helper/signal.h"

#include "priv.h"

int ecmg_stopping(const ecmg_client_t *c) {
  return atomic_load_explicit(&c->stop, memory_order_relaxed) || signal_stop_requested();
}

int ecmg_ecm_available_calc(ecmg_outage_mode_t outage_mode, int connected) {
  return !(outage_mode == ECMG_OUTAGE_SILENT && !connected);
}

int ecmg_client_get_ecm(ecmg_client_t *c, unsigned char *out, size_t cap, size_t *len_out) {
  int have, slot;
  if (!ecmg_ecm_available_calc(c->cfg.outage_mode, atomic_load_explicit(&c->connected, memory_order_relaxed)))
    return -1;
  slot = ecmg_client_target_parity(c);
  pthread_mutex_lock(&c->ecm_lock);
  have = c->ecm_slot_len[slot] > 0;
  if (have) {
    if (cap < c->ecm_slot_len[slot]) {
      pthread_mutex_unlock(&c->ecm_lock);
      return -1;
    }
    memcpy(out, c->ecm_slot[slot], c->ecm_slot_len[slot]);
    *len_out = c->ecm_slot_len[slot];
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

int ecmg_target_parity_calc(ecmg_outage_mode_t outage_mode, int connected, unsigned long packets_per_cp, unsigned long cur, unsigned long published_at, unsigned long last_parity) {
  unsigned long periods_elapsed;
  if (outage_mode != ECMG_OUTAGE_CYCLING || connected || packets_per_cp == 0)
    return (int)last_parity;
  periods_elapsed = (cur - published_at) / packets_per_cp;
  return (int)((last_parity + periods_elapsed) & 1UL);
}

int ecmg_client_connected(ecmg_client_t *c) { return atomic_load_explicit(&c->connected, memory_order_relaxed); }
unsigned long ecmg_client_cryptoperiod_transitions(ecmg_client_t *c) { return atomic_load_explicit(&c->cryptoperiod_transitions_total, memory_order_relaxed); }
unsigned long ecmg_client_ecm_total(ecmg_client_t *c) { return atomic_load_explicit(&c->ecm_total, memory_order_relaxed); }
unsigned long ecmg_client_ecm_errors(ecmg_client_t *c) { return atomic_load_explicit(&c->ecm_errors_total, memory_order_relaxed); }

int ecmg_client_target_parity(ecmg_client_t *c) {
  unsigned long last_parity = (unsigned long)atomic_load_explicit(&c->last_parity, memory_order_relaxed);
  unsigned long cur = atomic_load_explicit(c->packet_counter, memory_order_relaxed);
  unsigned long published_at = atomic_load_explicit(&c->cw_published_at, memory_order_relaxed);
  int connected = atomic_load_explicit(&c->connected, memory_order_relaxed);
  return ecmg_target_parity_calc(c->cfg.outage_mode, connected, c->packets_per_cp, cur, published_at, last_parity);
}

ecmg_client_t *ecmg_client_start(const ecmg_client_cfg_t *cfg, const atomic_ulong *packet_counter, unsigned long packets_per_cp, unsigned long lookahead_margin_packets) {
  cwenc_config_t cwenc_cfg;
  ecmg_client_t *c;

  if (cwenc_config_init(&cwenc_cfg, cfg->cwenc_algorithm, cfg->cwenc_aes_mode, cfg->cwenc_fixed_key_hex, cfg->cwenc_key_list_a_path, cfg->cwenc_key_list_b_path) != 0)
    return NULL;
  if (cwenc_config_validate(&cwenc_cfg, (int)scrambler_cw_len(cfg->algo)) != 0)
    return NULL;

  c = calloc(1, sizeof *c);
  if (!c)
    return NULL;
  c->cfg = *cfg;
  c->cw_len = scrambler_cw_len(cfg->algo);
  cwenc_ctx_init(&c->cwenc_ctx, &cwenc_cfg);
  c->packet_counter = packet_counter;
  c->packets_per_cp = packets_per_cp;
  c->lookahead_margin_packets = lookahead_margin_packets;
  pthread_mutex_init(&c->ecm_lock, NULL);
  if (pthread_create(&c->thread, NULL, ecmg_client_main, c) != 0) {
    log_line("ecmg: pthread_create: %s", strerror(errno));
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
  pthread_mutex_destroy(&c->ecm_lock);
  free(c);
}
