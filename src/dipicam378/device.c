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
  int cw_len; /* 8 (CSA2) or 16 (CISSA) */
  unsigned caid; /* 0 = unset, no CAID filtering */
};

device_state_t *device_state_new(const char *key_path, int cw_len, const char *serial, unsigned caid) {
  device_state_t *d = device_core_alloc(sizeof *d, key_path, serial, 0);
  if (!d) return NULL;
  d->cw_len = cw_len;
  d->caid = caid;
  return d;
}

void device_state_free(device_state_t *d) { device_core_free_state(d); }

void device_on_emm(device_state_t *d, const unsigned char *emm, size_t emm_len) {
  device_core_on_emm(&d->core, emm, emm_len, TOOL_NAME ": ");
}

/* oscam writes cw[0:8)/[8:16) as independent odd/even hw keys, skipping only an
   all-zero half - never duplicate, or every answer clobbers the other parity's key */
#define SC_SECTION_TID_ECM_ODD 0x81

int device_resolve_cw(device_state_t *d, const unsigned char *ecm, size_t ecm_len, unsigned srvid, unsigned caid, unsigned char cw_out[16]) {
  service_key_t *sk;
  unsigned char sk_copy[CRYPTO_KEY_LEN];
  unsigned char cw[16];
  int have;

  if (d->caid && caid != d->caid) return -2; /* permanent: wrong caid, never answerable regardless of EMM state */

  /* section header(3) + CP_CW_COMBINATION's cp_number(2), then encrypted CW block */
  if (ecm_len < 5 + CRYPTO_CW_ENC_LEN) return -1;
  device_core_lock(&d->core);
  sk = device_core_service_slot_locked(&d->core, srvid, 0);
  have = sk && sk->have;
  if (have) memcpy(sk_copy, sk->sk, sizeof sk_copy);
  device_core_unlock(&d->core);
  if (!have) return -1;

  if (device_ecm_decrypt(sk_copy, ecm + 5, d->cw_len, cw) != 0) {
    secure_zero(sk_copy, sizeof sk_copy);
    return -1;
  }
  secure_zero(sk_copy, sizeof sk_copy);

  memset(cw_out, 0, 16);
  if (d->cw_len == 16) {
    memcpy(cw_out, cw, 16);
  } else if (ecm[0] == SC_SECTION_TID_ECM_ODD) {
    memcpy(cw_out, cw, (size_t)d->cw_len);
  } else {
    memcpy(cw_out + 8, cw, (size_t)d->cw_len);
  }
  secure_zero(cw, sizeof cw);
  return 0;
}

unsigned device_state_services_active(device_state_t *d) { return device_core_services_active(&d->core); }
