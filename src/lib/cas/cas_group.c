/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>

#include "lib/log.h"
#include "lib/mux/cadescbuild.h"
#include "lib/mux/psi_build.h"

#include "cas_group.h"
#include "cas_scramble_engine.h"
#include "emmg_server.h"

#define CAS_GROUP_CW_HIST 8 /* rolling cache, keyed by shared epoch - lookahead depth across vendors */

typedef struct {
  int valid;
  unsigned long epoch;
  unsigned char cw[ECMG_MAX_CW_LEN];
} group_cw_hist_t;

typedef struct {
  cas_group_t *g;
  size_t idx;
} vendor_cw_ctx_t;

typedef struct {
  unsigned ca_system_id;
  unsigned ecm_pid;
  unsigned emm_pid;
  int required;

  ecmg_client_t *ecmg;
  emmg_server_t *emmg;
  double last_ecm_send;
  unsigned long epoch_base; /* this session's local cp_number 0 == group epoch epoch_base */
  vendor_cw_ctx_t cw_ctx;
} cas_group_vendor_t;

struct cas_group {
  cas_group_cfg_t cfg;
  unsigned char scrambling_mode;
  unsigned flush_pid;
  cas_scramble_engine_t *engine;

  double clock_wait_deadline;
  int have_clock;
  int fatal;
  unsigned lookahead_ms;
  atomic_ulong cp_clock_ms;

  pthread_mutex_t cw_lock;
  group_cw_hist_t hist[CAS_GROUP_CW_HIST];
  unsigned long cw_epoch_published;

  cas_group_vendor_t vendors[CAS_GROUP_MAX_VENDORS];
};

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int cw_gen(unsigned char *out, size_t len) {
  size_t got = 0;
  while (got < len) {
    ssize_t n = getrandom(out + got, len - got, 0);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      log_line("cas_group: getrandom: %s", strerror(errno));
      return -1;
    }
    got += (size_t)n;
  }
  return 0;
}

static unsigned long group_current_epoch(cas_group_t *g) {
  return atomic_load_explicit(&g->cp_clock_ms, memory_order_relaxed) / g->cfg.cp_duration_ms;
}

static int group_cw_for_epoch(cas_group_t *g, unsigned long epoch, unsigned char *out, size_t cw_len) {
  int idx = (int)(epoch % CAS_GROUP_CW_HIST);
  int rc = 0;

  pthread_mutex_lock(&g->cw_lock);
  if (!(g->hist[idx].valid && g->hist[idx].epoch == epoch)) {
    if (cw_gen(g->hist[idx].cw, cw_len) < 0) {
      rc = -1;
    } else {
      g->hist[idx].epoch = epoch;
      g->hist[idx].valid = 1;
    }
  }
  if (rc == 0)
    memcpy(out, g->hist[idx].cw, cw_len);
  pthread_mutex_unlock(&g->cw_lock);
  return rc;
}

/* ecmg_client's own cp_number is connection-local, resets on reconnect
   epoch_base anchors it to the shared epoch at the moment this vendor's channel came up. */
static int vendor_cw_get(void *ctx, unsigned short cp_number, unsigned char *cw_out, size_t cw_len) {
  vendor_cw_ctx_t *cc = ctx;
  cas_group_t *g = cc->g;
  unsigned long epoch = g->vendors[cc->idx].epoch_base + cp_number;
  return group_cw_for_epoch(g, epoch, cw_out, cw_len);
}

static void vendor_cw_connected(void *ctx) {
  vendor_cw_ctx_t *cc = ctx;
  cas_group_t *g = cc->g;
  g->vendors[cc->idx].epoch_base = group_current_epoch(g);
}

static int vendor_alive(cas_group_vendor_t *v) {
  return v->ecmg && ecmg_client_connected(v->ecmg);
}

int cas_group_fallback_active_calc(size_t vendor_count, const int *required, const int *alive) {
  size_t i;
  int any_alive = 0;
  for (i = 0; i < vendor_count; i++) {
    if (required[i] && !alive[i])
      return 1;
    if (alive[i])
      any_alive = 1;
  }
  return !any_alive;
}

static int cas_group_any_alive(cas_group_t *g) {
  size_t i;
  for (i = 0; i < g->cfg.vendor_count; i++)
    if (vendor_alive(&g->vendors[i]))
      return 1;
  return 0;
}

static int cas_group_fallback_active(cas_group_t *g) {
  int required[CAS_GROUP_MAX_VENDORS], alive[CAS_GROUP_MAX_VENDORS];
  size_t i;
  for (i = 0; i < g->cfg.vendor_count; i++) {
    required[i] = g->vendors[i].required;
    alive[i] = vendor_alive(&g->vendors[i]);
  }
  return cas_group_fallback_active_calc(g->cfg.vendor_count, required, alive);
}

static void cas_lazy_start(cas_group_t *g) {
  size_t i;
  for (i = 0; i < g->cfg.vendor_count; i++) {
    cas_group_vendor_t *v = &g->vendors[i];
    ecmg_client_cfg_t ecfg;
    emmg_server_cfg_t mcfg;
    const cas_group_vendor_cfg_t *vc = &g->cfg.vendors[i];

    memset(&ecfg, 0, sizeof ecfg);
    ecfg.host = vc->ecmg_host;
    ecfg.port = vc->ecmg_port;
    if (vc->ecmg_version) {
      ecfg.version_min = ecfg.version_max = (unsigned char)vc->ecmg_version;
    } else {
      ecfg.version_min = 2;
      ecfg.version_max = 3;
    }
    ecfg.super_cas_id = vc->super_cas_id;
    ecfg.ecm_id = vc->ecm_id;
    ecfg.cp_duration_ms = g->cfg.cp_duration_ms;
    ecfg.algo = g->cfg.algo;
    ecfg.outage_mode = vc->outage_mode;
    v->cw_ctx.g = g;
    v->cw_ctx.idx = i;
    ecfg.cw_source.get_cw = vendor_cw_get;
    ecfg.cw_source.on_connected = vendor_cw_connected;
    ecfg.cw_source.ctx = &v->cw_ctx;

    v->ecmg = ecmg_client_start(&ecfg, &g->cp_clock_ms, g->cfg.cp_duration_ms, g->lookahead_ms);
    if (!v->ecmg) {
      log_line("cas_group: ECMG client %zu failed to start", i);
      g->fatal = 1;
      continue;
    }

    memset(&mcfg, 0, sizeof mcfg);
    mcfg.port = vc->emmg_port;
    v->emmg = emmg_server_start(&mcfg);
    if (!v->emmg) {
      log_line("cas_group: EMMG server %zu failed to start", i);
      g->fatal = 1;
    }
  }
}

cas_group_t *cas_group_start(const cas_group_cfg_t *cfg, unsigned flush_pid) {
  cas_group_t *g;
  size_t i;
  unsigned lm;

  g = calloc(1, sizeof *g);
  if (!g)
    return NULL;
  g->cfg = *cfg;
  g->flush_pid = flush_pid;
  g->scrambling_mode = (cfg->algo == SCRAMBLE_ALGO_CISSA) ? CADESC_SCRAMBLING_MODE_CISSA : CADESC_SCRAMBLING_MODE_CSA2;
  g->engine = cas_scramble_engine_start(cfg->algo, cfg->pids, cfg->pid_count, flush_pid);
  if (!g->engine) {
    free(g);
    return NULL;
  }
  pthread_mutex_init(&g->cw_lock, NULL);
  for (i = 0; i < cfg->vendor_count; i++) {
    g->vendors[i].ca_system_id = cfg->vendors[i].super_cas_id >> 16;
    g->vendors[i].ecm_pid = cfg->vendors[i].ecm_pid;
    g->vendors[i].emm_pid = cfg->vendors[i].emm_pid;
    g->vendors[i].required = cfg->vendors[i].required;
    g->vendors[i].last_ecm_send = -1.0;
  }
  g->clock_wait_deadline = mono() + CAS_CORE_CLOCK_DETECT_TIMEOUT_S;
  lm = cfg->cp_duration_ms / 4;
  if (lm > 2000)
    lm = 2000;
  if (lm < 100)
    lm = 100;
  g->lookahead_ms = lm;
  return g;
}

void cas_group_stop(cas_group_t *g) {
  size_t i;
  if (!g)
    return;
  for (i = 0; i < g->cfg.vendor_count; i++) {
    if (g->vendors[i].ecmg)
      ecmg_client_stop(g->vendors[i].ecmg);
    if (g->vendors[i].emmg)
      emmg_server_stop(g->vendors[i].emmg);
  }
  cas_scramble_engine_stop(g->engine);
  pthread_mutex_destroy(&g->cw_lock);
  free(g);
}

int cas_group_failed(cas_group_t *g) { return g->fatal; }

void cas_group_tick_alive(cas_group_t *g) {
  if (!g->have_clock && !g->fatal && mono() >= g->clock_wait_deadline) {
    g->fatal = 1;
    log_line("cas_group: no clock activity observed within %.0fs, giving up", CAS_CORE_CLOCK_DETECT_TIMEOUT_S);
  }
}

void cas_group_clock_tick(cas_group_t *g, unsigned long delta_ms) {
  if (!g->have_clock) {
    g->have_clock = 1;
    cas_lazy_start(g);
    return;
  }
  atomic_fetch_add_explicit(&g->cp_clock_ms, delta_ms, memory_order_relaxed);
}

/* stops minting CWs once nobody is alive to deliver them as ECMs - engine keeps the last
   published CW (frozen), rather than rolling forward into keys no receiver can ever get. */
static void refresh_group_cw(cas_group_t *g, scrambler_emit_cb emit, void *ctx) {
  unsigned long epoch;
  unsigned char cw[ECMG_MAX_CW_LEN];
  size_t cw_len;

  if (!cas_group_any_alive(g))
    return;
  epoch = group_current_epoch(g);
  if (epoch == g->cw_epoch_published)
    return;
  cw_len = scrambler_cw_len(g->cfg.algo);
  if (group_cw_for_epoch(g, epoch, cw, cw_len) < 0)
    return;
  g->cw_epoch_published = epoch;
  cas_scramble_engine_set_cw(g->engine, (int)(epoch & 1), cw, cw_len, emit, ctx);
}

void cas_group_scramble_packet(cas_group_t *g, unsigned out_pid, double now, unsigned char pkt188[188], scrambler_emit_cb emit, void *ctx) {
  int have_target = 0;
  int target_parity;
  int cw_valid;
  size_t i;

  if (g->have_clock)
    refresh_group_cw(g, emit, ctx);

  for (i = 0; i < g->cfg.vendor_count; i++) {
    cas_group_vendor_t *v = &g->vendors[i];
    if (vendor_alive(v) && ecmg_client_ecm_epoch(v->ecmg) != 0) {
      have_target = 1;
      break;
    }
  }
  target_parity = (int)(g->cw_epoch_published & 1);
  cw_valid = !(g->cfg.fallback_clear && cas_group_fallback_active(g));

  cas_scramble_engine_scramble_packet(g->engine, out_pid, g->have_clock, have_target, target_parity, cw_valid, now, pkt188, emit, ctx);
}

void cas_group_flush(cas_group_t *g, scrambler_emit_cb emit, void *ctx) {
  if (g)
    cas_scramble_engine_flush(g->engine, emit, ctx);
}

size_t cas_group_prog_desc(cas_group_t *g, unsigned char *out, size_t cap) {
  size_t total = 0;
  size_t i;
  for (i = 0; i < g->cfg.vendor_count; i++) {
    size_t n = cadescbuild_ca_descriptor(g->vendors[i].ca_system_id, g->vendors[i].ecm_pid, out + total, cap - total);
    if (!n)
      return 0;
    total += n;
  }
  {
    size_t n = cadescbuild_scrambling_descriptor(g->scrambling_mode, out + total, cap - total);
    if (!n)
      return 0;
    total += n;
  }
  return total;
}

size_t cas_group_build_cat(cas_group_t *g, unsigned char *out, size_t cap) {
  unsigned char desc[CAS_GROUP_MAX_VENDORS * 16];
  size_t total = 0;
  size_t i;
  for (i = 0; i < g->cfg.vendor_count; i++) {
    size_t n = cadescbuild_ca_descriptor(g->vendors[i].ca_system_id, g->vendors[i].emm_pid, desc + total, sizeof desc - total);
    if (!n)
      return 0;
    total += n;
  }
  return psi_build_cat(0, desc, total, out, cap);
}

size_t cas_group_vendor_count(cas_group_t *g) { return g->cfg.vendor_count; }
unsigned cas_group_vendor_ecm_pid(cas_group_t *g, size_t idx) { return g->vendors[idx].ecm_pid; }
unsigned cas_group_vendor_emm_pid(cas_group_t *g, size_t idx) { return g->vendors[idx].emm_pid; }
unsigned cas_group_vendor_super_cas_id(cas_group_t *g, size_t idx) { return g->cfg.vendors[idx].super_cas_id; }

int cas_group_vendor_ecm_due(cas_group_t *g, size_t idx, double now, unsigned char *out, size_t cap, size_t *out_len) {
  cas_group_vendor_t *v = &g->vendors[idx];
  unsigned rep_ms;
  if (!v->ecmg)
    return -1;
  rep_ms = ecmg_client_ecm_rep_period_ms(v->ecmg);
  if (!rep_ms)
    rep_ms = 500;
  if (v->last_ecm_send >= 0.0 && now - v->last_ecm_send < (double)rep_ms / 1000.0)
    return -1;
  if (ecmg_client_get_ecm(v->ecmg, out, cap, out_len) < 0)
    return -1;
  v->last_ecm_send = now;
  return 0;
}

int cas_group_vendor_next_emm(cas_group_t *g, size_t idx, unsigned char *out, size_t cap, size_t *out_len) {
  cas_group_vendor_t *v = &g->vendors[idx];
  if (!v->emmg)
    return -1;
  return emmg_server_dequeue_emm(v->emmg, out, cap, out_len);
}

void cas_group_vendor_metrics(cas_group_t *g, size_t idx, cas_metrics_t *out) {
  cas_group_vendor_t *v = &g->vendors[idx];
  memset(out, 0, sizeof *out);
  if (v->ecmg) {
    out->ecmg_connected = ecmg_client_connected(v->ecmg);
    out->cryptoperiod_transitions_total = ecmg_client_cryptoperiod_transitions(v->ecmg);
    out->ecm_total = ecmg_client_ecm_total(v->ecmg);
    out->ecm_errors_total = ecmg_client_ecm_errors(v->ecmg);
  }
  if (v->emmg) {
    out->emmg_clients = emmg_server_client_count(v->emmg);
    out->emm_total = emmg_server_emm_total(v->emmg);
  }
  cas_scramble_engine_get_metrics(g->engine, &out->scrambled_packets_total, &out->unexpected_clear_packets_total);
}

void cas_group_shared_metrics(cas_group_t *g, unsigned long long *scrambled_packets_total, unsigned long long *unexpected_clear_packets_total) {
  cas_scramble_engine_get_metrics(g->engine, scrambled_packets_total, unexpected_clear_packets_total);
}
