/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_BISS_CA_STATE_H
#define DIPIDESCRAMBLE_BISS_CA_STATE_H

#include <stddef.h>

#include "lib/cas/biss/ca.h"

typedef struct biss_ca_state biss_ca_state_t;

/* NULL: privkey_path unreadable, or not an RSA key */
biss_ca_state_t *biss_ca_state_new(const char *privkey_path);
void biss_ca_state_free(biss_ca_state_t *s);

/* entitlement_session_id is learned from first EMM/ECM section seen, not pre-declared.
   this decodes 1 program at a time, so the CA_descriptor-resolved ECM/EMM pids already pin down which session.
   1: matched our entitlement_key_id, SK cache updated. 0: parse fail/esid mismatch/not ours/unchanged */
int biss_ca_state_on_emm(biss_ca_state_t *s, const unsigned char *emm, size_t emm_len);

/* 0 ok, sw_even_out/sw_odd_out filled. -1: parse fail/esid mismatch/SK for that parity not cached yet */
int biss_ca_state_resolve_ecm(biss_ca_state_t *s, const unsigned char *ecm, size_t ecm_len, unsigned char sw_even_out[BISS_CA_SW_LEN], unsigned char sw_odd_out[BISS_CA_SW_LEN]);

#endif
