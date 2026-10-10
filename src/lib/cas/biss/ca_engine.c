/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../sys/ioutil.h"
#include "../../helper/log.h"
#include "../../mux/cadescbuild.h"
#include "../../mux/psi_build.h"

#include "ca.h"
#include "ca_engine.h"
#include "ca_sections.h"

#define BISS_CA_MODE_CA_SYSTEM_ID 0x2610
#define BISS_CA_ENGINE_ENTRIES_PER_SECTION 15
#define BISS_CA_ENGINE_MAX_SECTIONS 256
#define BISS_CA_T_ECM_MIN_S 0.1 /* Tech 3292-s1 SS5 */
#define BISS_CA_T_EMM_MIN_S 0.2

typedef struct {
  unsigned char ekid[BISS_CA_EKID_LEN];
  biss_ca_key_t *pub;
} biss_ca_receiver_t;

typedef struct {
  biss_ca_receiver_t *v;
  size_t n;
  size_t cap;
} receiver_set_t;

typedef struct {
  unsigned char buf[4096];
  size_t len;
  double last_send;
} emm_section_t;

struct biss_ca_engine {
  cas_scramble_engine_t *scr;
  unsigned ecm_pid;
  unsigned emm_pid;
  unsigned esid;
  unsigned onid;

  char receivers_dir[512];
  receiver_set_t receivers;

  unsigned char sw[2][BISS_CA_SW_LEN]; /* indexed by SCRAMBLE_PARITY_EVEN/_ODD */
  unsigned long sw_epoch;              /* parity in force = sw_epoch & 1 */
  unsigned long sw_period_ms;
  unsigned long since_sw_change_ms;
  unsigned ecm_version;
  int ecm_dirty;
  unsigned char ecm_cache[128];
  size_t ecm_cache_len;
  double last_ecm_send;

  unsigned char sk[BISS_CA_SK_LEN];
  int sk_parity;
  unsigned long sk_epoch;
  unsigned long sk_period_ms;
  unsigned long since_sk_change_ms;
  unsigned emm_version;
  int emm_dirty;
  emm_section_t *emm_secs;
  size_t n_emm_secs;
  size_t emm_cursor;
};

static void set_free(receiver_set_t *s) {
  for (size_t i = 0; i < s->n; i++) biss_ca_key_free(s->v[i].pub);
  free(s->v);
  memset(s, 0, sizeof *s);
}

static int set_has(const receiver_set_t *s, const unsigned char ekid[BISS_CA_EKID_LEN]) {
  for (size_t i = 0; i < s->n; i++) {
    if (memcmp(s->v[i].ekid, ekid, BISS_CA_EKID_LEN) == 0) return 1;
  }
  return 0;
}

typedef struct {
  receiver_set_t *set;
  const char *path;
  int dups;
  int over;
} load_ctx_t;

static int load_visit(biss_ca_key_t *k, void *p) {
  load_ctx_t *lc = p;
  receiver_set_t *s = lc->set;
  unsigned char ekid[BISS_CA_EKID_LEN];

  if (biss_ca_entitlement_key_id(k, ekid) != 0) {
    log_line("biss-ca: entitlement_key_id derivation failed for a key in %s, skipping", lc->path);
    biss_ca_key_free(k);
    return 0;
  }
  if (set_has(s, ekid)) {
    lc->dups++;
    biss_ca_key_free(k);
    return 0;
  }
  if (s->n >= (size_t)BISS_CA_ENGINE_MAX_SECTIONS * BISS_CA_ENGINE_ENTRIES_PER_SECTION) {
    lc->over++;
    biss_ca_key_free(k);
    return 0;
  }
  if (s->n == s->cap) {
    size_t cap = s->cap ? s->cap * 2 : 16;
    biss_ca_receiver_t *v = realloc(s->v, cap * sizeof *v);
    if (!v) {
      biss_ca_key_free(k);
      return 1;
    }
    s->v = v;
    s->cap = cap;
  }
  memcpy(s->v[s->n].ekid, ekid, BISS_CA_EKID_LEN);
  s->v[s->n].pub = k;
  s->n++;
  return 0;
}

static size_t load_receivers(const char *dir, receiver_set_t *out) {
  DIR *d;
  const struct dirent *ent;
  load_ctx_t lc = {out, NULL, 0, 0};
  int skipped_bad = 0;

  d = opendir(dir);
  if (!d) {
    log_line("biss-ca: cannot open --biss2-ca-receivers %s: %s", dir, strerror(errno));
    return 0;
  }
  while ((ent = readdir(d)) != NULL) {
    char path[768];
    if (ent->d_name[0] == '.') continue;
    snprintf(path, sizeof path, "%s/%s", dir, ent->d_name);
    lc.path = path;
    if (biss_ca_key_foreach_file(path, 0, load_visit, &lc) <= 0) {
      log_line("biss-ca: %s holds no readable PEM public key, skipping", path);
      skipped_bad++;
    }
  }
  closedir(d);
  if (lc.over) log_line("biss-ca: %d key(s) in %s dropped, past the %d-section EMM limit", lc.over, dir, BISS_CA_ENGINE_MAX_SECTIONS);
  if (lc.dups) log_line("biss-ca: %d duplicate key(s) in %s ignored", lc.dups, dir);
  if (skipped_bad) log_line("biss-ca: %d file(s) in %s not usable as receiver keys", skipped_bad, dir);
  log_line("biss-ca: loaded %zu entitled receiver(s) from %s", out->n, dir);
  return out->n;
}

biss_ca_engine_t *biss_ca_engine_start(const biss_ca_engine_cfg_t *cfg) {
  biss_ca_engine_t *e;

  if (!cfg || !cfg->receivers_dir || cfg->sw_period_ms < 1000) return NULL;
  e = calloc(1, sizeof *e);
  if (!e) return NULL;

  bufcpy(e->receivers_dir, sizeof e->receivers_dir, cfg->receivers_dir);
  e->esid = cfg->esid;
  e->onid = cfg->onid;
  e->ecm_pid = cfg->ecm_pid;
  e->emm_pid = cfg->emm_pid;
  e->sw_period_ms = cfg->sw_period_ms;
  e->sk_period_ms = cfg->sw_period_ms * 6; /* SK outlives several SW periods, spec T_EMM_change > T_ECM_change */
  e->last_ecm_send = -1.0;

  if (load_receivers(e->receivers_dir, &e->receivers) == 0) {
    set_free(&e->receivers);
    free(e);
    return NULL;
  }

  e->scr = cas_scramble_engine_start(SCRAMBLE_ALGO_CISSA, cfg->pids, cfg->pid_count, cfg->flush_pid);
  if (!e->scr) {
    set_free(&e->receivers);
    free(e);
    return NULL;
  }

  if (biss_ca_random(e->sw[SCRAMBLE_PARITY_EVEN], BISS_CA_SW_LEN) != 0 ||
      biss_ca_random(e->sw[SCRAMBLE_PARITY_ODD], BISS_CA_SW_LEN) != 0 ||
      biss_ca_random(e->sk, BISS_CA_SK_LEN) != 0) {
    log_line("biss-ca: initial key generation failed (RNG unavailable)");
    cas_scramble_engine_stop(e->scr);
    set_free(&e->receivers);
    free(e);
    return NULL;
  }
  if (cas_scramble_engine_set_cw(e->scr, SCRAMBLE_PARITY_EVEN, e->sw[SCRAMBLE_PARITY_EVEN], BISS_CA_SW_LEN, NULL, NULL) != 0 ||
      cas_scramble_engine_set_cw(e->scr, SCRAMBLE_PARITY_ODD, e->sw[SCRAMBLE_PARITY_ODD], BISS_CA_SW_LEN, NULL, NULL) != 0) {
    log_line("biss-ca: initial key rejected by the scrambler, not starting");
    cas_scramble_engine_stop(e->scr);
    set_free(&e->receivers);
    free(e);
    return NULL;
  }
  e->ecm_dirty = 1;
  e->emm_dirty = 1;

  log_line("biss-ca: active, esid=0x%04x ecm_pid=0x%04x emm_pid=0x%04x, %zu receiver(s)", e->esid, e->ecm_pid, e->emm_pid, e->receivers.n);
  return e;
}

void biss_ca_engine_stop(biss_ca_engine_t *e) {
  if (!e) return;
  cas_scramble_engine_stop(e->scr);
  set_free(&e->receivers);
  free(e->emm_secs);
  free(e);
}

int biss_ca_engine_add_pid(biss_ca_engine_t *e, unsigned pid) { return cas_scramble_engine_add_pid(e->scr, pid); }

void biss_ca_engine_clock_tick(biss_ca_engine_t *e, unsigned long delta_ms) {
  e->since_sw_change_ms += delta_ms;
  e->since_sk_change_ms += delta_ms;
}

static void rotate_sw(biss_ca_engine_t *e, scrambler_emit_cb emit, void *ctx) {
  unsigned long epoch = e->sw_epoch + 1;
  int parity = (int)(epoch & 1);
  unsigned char fresh[BISS_CA_SW_LEN];

  if (biss_ca_random(fresh, BISS_CA_SW_LEN) != 0) {
    log_line("biss-ca: SW rotation failed (RNG unavailable), keeping current key");
    return;
  }
  memcpy(e->sw[parity], fresh, BISS_CA_SW_LEN);
  e->sw_epoch = epoch;
  e->ecm_version = (e->ecm_version + 1) & 0x1F;
  e->ecm_dirty = 1;
  cas_scramble_engine_set_cw(e->scr, parity, e->sw[parity], BISS_CA_SW_LEN, emit, ctx);
}

static void rotate_sk(biss_ca_engine_t *e) {
  unsigned long epoch = e->sk_epoch + 1;
  unsigned char fresh[BISS_CA_SK_LEN];

  if (biss_ca_random(fresh, BISS_CA_SK_LEN) != 0) {
    log_line("biss-ca: SK rotation failed (RNG unavailable), keeping current key");
    return;
  }
  memcpy(e->sk, fresh, BISS_CA_SK_LEN);
  e->sk_parity = (int)(epoch & 1);
  e->sk_epoch = epoch;
  e->emm_version = (e->emm_version + 1) & 0x1F;
  e->emm_dirty = 1;
  e->ecm_dirty = 1; /* ECM re-encrypts SW under the new SK */
}

void biss_ca_engine_force_sk_rotation(biss_ca_engine_t *e) {
  if (e->since_sk_change_ms < e->sk_period_ms)
    e->since_sk_change_ms = e->sk_period_ms;
}

void biss_ca_engine_scramble_packet(biss_ca_engine_t *e, unsigned out_pid, double now, unsigned char pkt188[188], scrambler_emit_cb emit, void *ctx) {
  int target_parity;

  if (e->since_sw_change_ms >= e->sw_period_ms) {
    rotate_sw(e, emit, ctx);
    e->since_sw_change_ms = 0;
  }
  if (e->since_sk_change_ms >= e->sk_period_ms) {
    rotate_sk(e);
    e->since_sk_change_ms = 0;
  }
  target_parity = (int)(e->sw_epoch & 1);
  cas_scramble_engine_scramble_packet(e->scr, out_pid, 1, 1, target_parity, 1, now, pkt188, emit, ctx);
}

void biss_ca_engine_flush(biss_ca_engine_t *e, scrambler_emit_cb emit, void *ctx) {
  cas_scramble_engine_flush(e->scr, emit, ctx);
}

void biss_ca_engine_get_metrics(biss_ca_engine_t *e, unsigned long long *scrambled_packets_total, unsigned long long *unexpected_clear_packets_total) {
  cas_scramble_engine_get_metrics(e->scr, scrambled_packets_total, unexpected_clear_packets_total);
}

unsigned biss_ca_engine_ecm_pid(const biss_ca_engine_t *e) { return e->ecm_pid; }
unsigned biss_ca_engine_emm_pid(const biss_ca_engine_t *e) { return e->emm_pid; }
size_t biss_ca_engine_receiver_count(const biss_ca_engine_t *e) { return e->receivers.n; }

size_t biss_ca_engine_prog_desc(const biss_ca_engine_t *e, unsigned char *out, size_t cap) {
  unsigned char priv[8];
  size_t priv_len;
  size_t n;
  size_t n2;

  priv_len = biss_ca_build_entitlement_session_id_desc(e->esid, e->onid, priv, sizeof priv);
  if (!priv_len) return 0;
  n = cadescbuild_ca_descriptor_priv(BISS_CA_MODE_CA_SYSTEM_ID, e->ecm_pid, priv, priv_len, out, cap);
  if (!n) return 0;
  n2 = cadescbuild_scrambling_descriptor(CADESC_SCRAMBLING_MODE_CISSA, out + n, cap - n);
  if (!n2) return 0;
  n += n2;
  return n;
}

size_t biss_ca_engine_build_cat(const biss_ca_engine_t *e, unsigned char *out, size_t cap) {
  unsigned char priv[8];
  unsigned char desc[16];
  size_t priv_len;
  size_t desc_len;

  priv_len = biss_ca_build_entitlement_session_id_desc(e->esid, e->onid, priv, sizeof priv);
  if (!priv_len) return 0;
  desc_len = cadescbuild_ca_descriptor_priv(BISS_CA_MODE_CA_SYSTEM_ID, e->emm_pid, priv, priv_len, desc, sizeof desc);
  if (!desc_len) return 0;
  return psi_build_cat(0, desc, desc_len, out, cap);
}

/* rebuild only on real content change (dirty) - repeats stay byte-identical, matching
   "IV regenerated only when the ECM payload is updated" (Tech 3292-s1 SS4.2.2.5.5) */
static void rebuild_ecm(biss_ca_engine_t *e) {
  unsigned char iv[BISS_CA_IV_LEN];
  unsigned char esw_even[BISS_CA_SW_LEN];
  unsigned char esw_odd[BISS_CA_SW_LEN];
  size_t len;

  if (biss_ca_random(iv, BISS_CA_IV_LEN) != 0) {
    log_line("biss-ca: ECM rebuild failed (RNG unavailable)");
    return;
  }
  if (biss_ca_aes_cbc_encrypt(e->sk, iv, e->sw[SCRAMBLE_PARITY_EVEN], esw_even) != 0 || biss_ca_aes_cbc_encrypt(e->sk, iv, e->sw[SCRAMBLE_PARITY_ODD], esw_odd) != 0) {
    log_line("biss-ca: ECM rebuild failed (AES-CBC)");
    return;
  }
  len = biss_ca_build_ecm_section(e->esid, e->ecm_version, 0, 0, e->onid, e->sk_parity, iv, esw_even, esw_odd, e->ecm_cache, sizeof e->ecm_cache);
  if (!len) {
    log_line("biss-ca: ECM section build overflow");
    return;
  }
  e->ecm_cache_len = len;
  e->ecm_dirty = 0;
}

static void rebuild_emm(biss_ca_engine_t *e) {
  unsigned char session_data[BISS_CA_SESSION_DATA_LEN];
  biss_ca_emm_entry_t *entries;
  emm_section_t *secs;
  size_t n = 0;
  size_t n_secs;

  if (!biss_ca_build_session_data(e->sk, e->sk_parity, session_data, sizeof session_data)) {
    log_line("biss-ca: EMM rebuild failed (session_data build)");
    return;
  }
  entries = malloc((e->receivers.n ? e->receivers.n : 1) * sizeof *entries);
  if (!entries) return;
  for (size_t i = 0; i < e->receivers.n; i++) {
    memcpy(entries[n].entitlement_key_id, e->receivers.v[i].ekid, BISS_CA_EKID_LEN);
    if (biss_ca_rsa_encrypt(e->receivers.v[i].pub, session_data, sizeof session_data, entries[n].encrypted_session_data) != 0) {
      log_line("biss-ca: RSA-OAEP encrypt failed for one receiver, dropping it this round");
      continue;
    }
    n++;
  }

  n_secs = n ? (n + BISS_CA_ENGINE_ENTRIES_PER_SECTION - 1) / BISS_CA_ENGINE_ENTRIES_PER_SECTION : 1;
  secs = realloc(e->emm_secs, n_secs * sizeof *secs);
  if (!secs) {
    free(entries);
    return;
  }
  e->emm_secs = secs;
  for (size_t i = e->n_emm_secs; i < n_secs; i++) {
    secs[i].len = 0;
    secs[i].last_send = -1.0;
  }
  for (size_t i = 0; i < n_secs; i++) {
    size_t first = i * BISS_CA_ENGINE_ENTRIES_PER_SECTION;
    size_t cnt = n - first < BISS_CA_ENGINE_ENTRIES_PER_SECTION ? n - first : BISS_CA_ENGINE_ENTRIES_PER_SECTION;
    secs[i].len = biss_ca_build_emm_section(0x81, e->esid, e->emm_version, (unsigned)i, (unsigned)(n_secs - 1), e->onid, 0x81, entries + first, cnt, secs[i].buf, sizeof secs[i].buf);
    if (!secs[i].len) {
      log_line("biss-ca: EMM section %zu build overflow (%zu receivers)", i, n);
      free(entries);
      return;
    }
  }
  free(entries);
  e->n_emm_secs = n_secs;
  e->emm_cursor = 0;
  e->emm_dirty = 0;
}

int biss_ca_engine_ecm_due(biss_ca_engine_t *e, double now, unsigned char *out, size_t cap, size_t *out_len) {
  if (e->ecm_dirty) rebuild_ecm(e);
  if (e->ecm_cache_len == 0 || e->ecm_cache_len > cap) return -1;
  if (e->last_ecm_send >= 0.0 && now - e->last_ecm_send < BISS_CA_T_ECM_MIN_S) return -1;
  memcpy(out, e->ecm_cache, e->ecm_cache_len);
  *out_len = e->ecm_cache_len;
  e->last_ecm_send = now;
  return 0;
}

int biss_ca_engine_emm_due(biss_ca_engine_t *e, double now, unsigned char *out, size_t cap, size_t *out_len) {
  if (e->emm_dirty) rebuild_emm(e);
  for (size_t k = 0; k < e->n_emm_secs; k++) {
    size_t i = (e->emm_cursor + k) % e->n_emm_secs;
    emm_section_t *s = &e->emm_secs[i];
    if (s->len == 0 || s->len > cap) continue;
    if (s->last_send >= 0.0 && now - s->last_send < BISS_CA_T_EMM_MIN_S) continue;
    memcpy(out, s->buf, s->len);
    *out_len = s->len;
    s->last_send = now;
    e->emm_cursor = (i + 1) % e->n_emm_secs;
    return 0;
  }
  return -1;
}

int biss_ca_engine_reload_receivers(biss_ca_engine_t *e) {
  receiver_set_t fresh = {0};
  int changed;

  if (load_receivers(e->receivers_dir, &fresh) == 0) {
    log_line("biss-ca: reload of %s produced zero usable receivers, keeping the previous list", e->receivers_dir);
    set_free(&fresh);
    return -1;
  }

  changed = (fresh.n != e->receivers.n);
  for (size_t i = 0; !changed && i < fresh.n; i++) changed = !set_has(&e->receivers, fresh.v[i].ekid);
  for (size_t i = 0; !changed && i < e->receivers.n; i++) changed = !set_has(&fresh, e->receivers.v[i].ekid);
  set_free(&e->receivers);
  e->receivers = fresh;
  e->emm_dirty = 1;
  return changed;
}
