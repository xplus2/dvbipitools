/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/cas/device_state_core.h"
#include "lib/log.h"
#include "lib/secure_zero.h"

#include "crypto.h"
#include "device.h"
#include "version.h"

struct device_state {
  device_core_t core;
  ecm_profile_t profile;
};

device_state_t *device_state_new(const char *key_path, const char *serial, const ecm_profile_t *profile) {
  device_state_t *d;
  size_t serial_len = strlen(serial);

  if (serial_len == 0 || serial_len >= DEVICE_SERIAL_MAX)
    return NULL;
  d = calloc(1, sizeof *d);
  if (!d)
    return NULL;
  if (device_core_init(&d->core, key_path, serial) != 0) {
    free(d);
    return NULL;
  }
  d->profile = *profile;
  return d;
}

void device_state_free(device_state_t *d) {
  if (!d)
    return;
  device_core_release(&d->core);
  free(d);
}

int device_on_emm(device_state_t *d, const unsigned char *emm, size_t emm_len) {
  return device_core_on_emm(&d->core, emm, emm_len, TOOL_NAME ": ");
}

int device_resolve_cw(device_state_t *d, const unsigned char *ecm, size_t ecm_len, unsigned srvid, int cw_len, unsigned ecm_pid, unsigned char cw_out[16]) {
  service_key_t *sk;
  unsigned char cw[16];
  unsigned cp_number;

  if (cw_len != 8 && cw_len != 16)
    return -1;
  /* section header(3) + CP_CW_COMBINATION's cp_number(2), then profile's (or legacy fixed) payload */
  if (ecm_len < 5)
    return -1;
  /* srvid = local PAT program_number, not CAS's service_id. MPTS CW is mux-wide,
     --sid unrelated. one session per process: lone cached key unambiguous. */
  sk = device_core_service_slot(&d->core, srvid, 0);
  if (!sk && d->core.service_count == 1)
    sk = &d->core.services[0];
  if (!sk || !sk->have)
    return -1;

  cp_number = ((unsigned)ecm[3] << 8) | ecm[4];
  if (d->profile.set) {
    ecm_cw_combo_t combos[ECM_PROFILE_CW_MAX];
    int combo_count = 0;
    if (ecm_profile_decrypt_cw(&d->profile, cw_len, sk->sk, ecm + 5, ecm_len - 5, cp_number, ecm_pid, combos, &combo_count) != 0) {
      log_line(TOOL_NAME ": ecm_profile: decrypt or integrity check failed");
      return -1;
    }
    memcpy(cw, combos[0].cw, (size_t)cw_len);
  } else {
    if (ecm_len < 5 + CRYPTO_CW_ENC_LEN)
      return -1;
    if (device_ecm_decrypt(sk->sk, ecm + 5, cw_len, cw) != 0)
      return -1;
  }

  if (cw_len == 16) {
    memcpy(cw_out, cw, 16);
  } else {
    memcpy(cw_out, cw, (size_t)cw_len);
    memcpy(cw_out + cw_len, cw, (size_t)cw_len);
  }
  secure_zero(cw, sizeof cw);
  return 0;
}
