/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/cas/biss/ca_sections.h"

#include "biss_ca_state.h"

struct biss_ca_state {
  biss_ca_key_t *priv;
  unsigned char ekid[BISS_CA_EKID_LEN];
  int have_esid;
  unsigned esid;
  unsigned char sk[2][BISS_CA_SK_LEN]; /* indexed by session_key_parity */
  int have_sk[2];
};

biss_ca_state_t *biss_ca_state_new(const char *privkey_path) {
  biss_ca_state_t *s = calloc(1, sizeof *s);
  if (!s)
    return NULL;
  s->priv = biss_ca_key_load_private_file(privkey_path);
  if (!s->priv) {
    free(s);
    return NULL;
  }
  if (biss_ca_entitlement_key_id(s->priv, s->ekid) != 0) {
    biss_ca_key_free(s->priv);
    free(s);
    return NULL;
  }
  return s;
}

void biss_ca_state_free(biss_ca_state_t *s) {
  if (!s)
    return;
  biss_ca_key_free(s->priv);
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
  const unsigned char *enc;
  unsigned char session_data[BISS_CA_SESSION_DATA_MAX];
  unsigned char sk[BISS_CA_SK_LEN];
  size_t sdlen;
  int parity;

  if (biss_ca_parse_emm_section(emm, emm_len, &parsed) != 0)
    return 0;
  if (!esid_matches_or_learn(s, parsed.esid))
    return 0;
  enc = biss_ca_emm_find_entry(&parsed, s->ekid);
  if (!enc)
    return 0;
  if (biss_ca_rsa_decrypt(s->priv, enc, session_data, sizeof session_data, &sdlen) != 0)
    return 0;
  if (biss_ca_parse_session_data(session_data, sdlen, sk, &parity) != 0)
    return 0;
  if (s->have_sk[parity] && memcmp(s->sk[parity], sk, BISS_CA_SK_LEN) == 0)
    return 0; /* unchanged */
  memcpy(s->sk[parity], sk, BISS_CA_SK_LEN);
  s->have_sk[parity] = 1;
  return 1;
}

int biss_ca_state_resolve_ecm(biss_ca_state_t *s, const unsigned char *ecm, size_t ecm_len, unsigned char sw_even_out[BISS_CA_SW_LEN], unsigned char sw_odd_out[BISS_CA_SW_LEN]) {
  biss_ca_ecm_parsed_t parsed;

  if (biss_ca_parse_ecm_section(ecm, ecm_len, &parsed) != 0)
    return -1;
  if (!esid_matches_or_learn(s, parsed.esid))
    return -1;
  if (!s->have_sk[parsed.session_key_parity])
    return -1;
  if (biss_ca_aes_cbc_decrypt(s->sk[parsed.session_key_parity], parsed.iv, parsed.esw_even, sw_even_out) != 0)
    return -1;
  if (biss_ca_aes_cbc_decrypt(s->sk[parsed.session_key_parity], parsed.iv, parsed.esw_odd, sw_odd_out) != 0)
    return -1;
  return 0;
}
