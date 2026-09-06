/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/cas/device_state_core.h"
#include "lib/helper/log.h"
#include "lib/helper/secure_zero.h"

#include "crypto.h"
#include "device.h"
#include "version.h"

struct device_state {
  device_core_t core;
  ecm_profile_t profile;
};

device_state_t *device_state_new(const char *key_path, const char *serial, const ecm_profile_t *profile, size_t max_services) {
  device_state_t *d;

  if (strlen(serial) == 0) return NULL;
  d = device_core_alloc(sizeof *d, key_path, serial, max_services);
  if (!d) return NULL;
  d->profile = *profile;
  return d;
}

void device_state_free(device_state_t *d) { device_core_free_state(d); }

int device_on_emm(device_state_t *d, const unsigned char *emm, size_t emm_len) {
  return device_core_on_emm(&d->core, emm, emm_len, TOOL_NAME ": ");
}

int device_resolve_cw(device_state_t *d, const unsigned char *ecm, size_t ecm_len, unsigned srvid, int cw_len, unsigned ecm_pid, unsigned char cw_out[16]) {
  service_key_t *sk;
  unsigned char sk_copy[CRYPTO_KEY_LEN];
  unsigned char cw[16];
  unsigned cp_number;
  int have;

  if (cw_len != 8 && cw_len != 16) return -1;
  /* section header(3) + CP_CW_COMBINATION's cp_number(2), then profile's (or legacy fixed) payload */
  if (ecm_len < 5) return -1;
  device_core_lock(&d->core);
  /* srvid = local PAT program_number, not CAS's service_id. MPTS CW is mux-wide,
     --sid unrelated. one session per process: lone cached key unambiguous. */
  sk = device_core_service_slot_locked(&d->core, srvid, 0);
  if (!sk && d->core.service_count == 1) sk = &d->core.services[0];
  have = sk && sk->have;
  if (have) memcpy(sk_copy, sk->sk, sizeof sk_copy);
  device_core_unlock(&d->core);
  if (!have) return -1;

  cp_number = ((unsigned)ecm[3] << 8) | ecm[4];
  if (d->profile.set) {
    ecm_cw_combo_t combos[ECM_PROFILE_CW_MAX];
    int combo_count = 0;
    if (ecm_profile_decrypt_cw(&d->profile, cw_len, sk_copy, ecm + 5, ecm_len - 5, cp_number, ecm_pid, combos, &combo_count) != 0) {
      log_line(TOOL_NAME ": ecm_profile: decrypt or integrity check failed");
      secure_zero(sk_copy, sizeof sk_copy);
      return -1;
    }
    memcpy(cw, combos[0].cw, (size_t)cw_len);
  } else {
    if (ecm_len < 5 + CRYPTO_CW_ENC_LEN) {
      secure_zero(sk_copy, sizeof sk_copy);
      return -1;
    }
    if (device_ecm_decrypt(sk_copy, ecm + 5, cw_len, cw) != 0) {
      secure_zero(sk_copy, sizeof sk_copy);
      return -1;
    }
  }
  secure_zero(sk_copy, sizeof sk_copy);
  if (cw_len == 16) {
    memcpy(cw_out, cw, 16);
  } else {
    memcpy(cw_out, cw, (size_t)cw_len);
    memcpy(cw_out + cw_len, cw, (size_t)cw_len);
  }
  secure_zero(cw, sizeof cw);
  return 0;
}
