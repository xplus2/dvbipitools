/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "lib/cas/biss/ca_sections.h"
#include "lib/helper/log.h"

#include "biss_ca_state.h"

typedef struct {
  biss_ca_key_t *priv;
  unsigned char ekid[BISS_CA_EKID_LEN];
} held_key_t;

struct biss_ca_state {
  held_key_t *keys;
  size_t n_keys;
  size_t cap_keys;
  int have_esid;
  unsigned esid;
  unsigned char sk[2][BISS_CA_SK_LEN]; /* indexed by session_key_parity */
  int have_sk[2];
};

static int holds(const biss_ca_state_t *s, const unsigned char ekid[BISS_CA_EKID_LEN]) {
  for (size_t i = 0; i < s->n_keys; i++) if (memcmp(s->keys[i].ekid, ekid, BISS_CA_EKID_LEN) == 0) return 1;
  return 0;
}

static int add_key(biss_ca_key_t *k, void *ctx) {
  biss_ca_state_t *s = ctx;
  unsigned char ekid[BISS_CA_EKID_LEN];

  if (biss_ca_entitlement_key_id(k, ekid) != 0 || holds(s, ekid)) {
    biss_ca_key_free(k);
    return 0;
  }
  if (s->n_keys == s->cap_keys) {
    size_t cap = s->cap_keys ? s->cap_keys * 2 : 8;
    held_key_t *v = realloc(s->keys, cap * sizeof *v);
    if (!v) {
      biss_ca_key_free(k);
      return 1;
    }
    s->keys = v;
    s->cap_keys = cap;
  }
  s->keys[s->n_keys].priv = k;
  memcpy(s->keys[s->n_keys].ekid, ekid, BISS_CA_EKID_LEN);
  s->n_keys++;
  return 0;
}

static void load_path(biss_ca_state_t *s, const char *path) {
  struct stat st;
  DIR *d;
  const struct dirent *ent;

  if (stat(path, &st) != 0) {
    log_line("biss-ca: cannot access %s", path);
    return;
  }
  if (!S_ISDIR(st.st_mode)) {
    if (biss_ca_key_foreach_file(path, 1, add_key, s) <= 0)
      log_line("biss-ca: %s holds no readable PEM private key", path);
    return;
  }
  d = opendir(path);
  if (!d) {
    log_line("biss-ca: cannot open directory %s", path);
    return;
  }
  while ((ent = readdir(d)) != NULL) {
    char file[1024];
    if (ent->d_name[0] == '.') continue;
    snprintf(file, sizeof file, "%s/%s", path, ent->d_name);
    if (stat(file, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    if (biss_ca_key_foreach_file(file, 1, add_key, s) <= 0)
      log_line("biss-ca: %s holds no readable PEM private key, skipping", file);
  }
  closedir(d);
}

biss_ca_state_t *biss_ca_state_new(const char *const *paths, size_t n_paths) {
  biss_ca_state_t *s = calloc(1, sizeof *s);
  if (!s)
    return NULL;
  for (size_t i = 0; i < n_paths; i++)
    load_path(s, paths[i]);
  if (s->n_keys == 0) {
    biss_ca_state_free(s);
    return NULL;
  }
  log_line("biss-ca: %zu private key(s) loaded", s->n_keys);
  return s;
}

void biss_ca_state_free(biss_ca_state_t *s) {
  if (!s) return;
  for (size_t i = 0; i < s->n_keys; i++) biss_ca_key_free(s->keys[i].priv);
  free(s->keys);
  free(s);
}

static int esid_matches_or_learn(biss_ca_state_t *s, unsigned esid) {
  if (!s->have_esid) {
    s->have_esid = 1;
    s->esid = esid;
    return 1;
  }
  return s->esid == esid;
}

int biss_ca_state_on_emm(biss_ca_state_t *s, const unsigned char *emm, size_t emm_len) {
  biss_ca_emm_parsed_t parsed;
  if (biss_ca_parse_emm_section(emm, emm_len, &parsed) != 0) return 0;
  if (!esid_matches_or_learn(s, parsed.esid)) return 0;
  for (size_t i = 0; i < s->n_keys; i++) {
    const unsigned char *enc = biss_ca_emm_find_entry(&parsed, s->keys[i].ekid);
    unsigned char session_data[BISS_CA_SESSION_DATA_MAX];
    unsigned char sk[BISS_CA_SK_LEN];
    size_t sdlen;
    int parity;
    if (!enc) continue;
    if (biss_ca_rsa_decrypt(s->keys[i].priv, enc, session_data, sizeof session_data, &sdlen) != 0) continue;
    if (biss_ca_parse_session_data(session_data, sdlen, sk, &parity) != 0) continue;
    if (s->have_sk[parity] && memcmp(s->sk[parity], sk, BISS_CA_SK_LEN) == 0) return 0; /* unchanged */
    memcpy(s->sk[parity], sk, BISS_CA_SK_LEN);
    s->have_sk[parity] = 1;
    return 1;
  }
  return 0;
}

int biss_ca_state_resolve_ecm(biss_ca_state_t *s, const unsigned char *ecm, size_t ecm_len, unsigned char sw_even_out[BISS_CA_SW_LEN], unsigned char sw_odd_out[BISS_CA_SW_LEN]) {
  biss_ca_ecm_parsed_t parsed;

  if (biss_ca_parse_ecm_section(ecm, ecm_len, &parsed) != 0) return -1;
  if (!esid_matches_or_learn(s, parsed.esid)) return -1;
  if (!s->have_sk[parsed.session_key_parity]) return -1;
  if (biss_ca_aes_cbc_decrypt(s->sk[parsed.session_key_parity], parsed.iv, parsed.esw_even, sw_even_out) != 0) return -1;
  if (biss_ca_aes_cbc_decrypt(s->sk[parsed.session_key_parity], parsed.iv, parsed.esw_odd, sw_odd_out) != 0) return -1;
  return 0;
}
