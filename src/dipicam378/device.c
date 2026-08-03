/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/log.h"

#include "crypto.h"
#include "device.h"
#include "version.h"

#define DEVICE_MAX_SERVICES 32

typedef struct {
  unsigned service_id;
  unsigned char sk[CRYPTO_KEY_LEN];
  int have;
} service_key_t;

#define DEVICE_SERIAL_MAX 256 /* addr_len is one wire byte, max 255 + nul */

struct device_state {
  EVP_PKEY *ek;
  int cw_len; /* 8 (CSA2) or 16 (CISSA) */
  char serial[DEVICE_SERIAL_MAX];
  size_t serial_len;
  unsigned caid; /* 0 = unset, no CAID filtering */
  unsigned char bk[CRYPTO_KEY_LEN];
  int have_bk;
  service_key_t services[DEVICE_MAX_SERVICES];
  size_t service_count;
};

device_state_t *device_state_new(const char *key_path, int cw_len, const char *serial, unsigned caid) {
  device_state_t *d;
  size_t serial_len = serial ? strlen(serial) : 0;

  if (serial_len >= sizeof d->serial)
    return NULL;
  d = calloc(1, sizeof *d);
  if (!d)
    return NULL;
  if (device_key_load(key_path, &d->ek) != 0) {
    free(d);
    return NULL;
  }
  d->cw_len = cw_len;
  if (serial_len)
    memcpy(d->serial, serial, serial_len);
  d->serial_len = serial_len;
  d->caid = caid;
  return d;
}

void device_state_free(device_state_t *d) {
  if (!d)
    return;
  EVP_PKEY_free(d->ek);
  free(d);
}

static service_key_t *service_slot(device_state_t *d, unsigned service_id, int create) {
  size_t i;
  for (i = 0; i < d->service_count; i++)
    if (d->services[i].service_id == service_id)
      return &d->services[i];
  if (!create || d->service_count >= DEVICE_MAX_SERVICES)
    return NULL;
  d->services[d->service_count].service_id = service_id;
  d->services[d->service_count].have = 0;
  return &d->services[d->service_count++];
}

static void handle_emm_u(device_state_t *d, const unsigned char *p, size_t len) {
  unsigned addr_len;
  const unsigned char *ct;
  size_t ct_len;

  if (len < 3)
    return;
  addr_len = p[0];
  if (len < 1 + addr_len + 2)
    return;
  if (d->serial_len && (addr_len != d->serial_len || memcmp(p + 1, d->serial, addr_len) != 0))
    return; /* not ours, skip - no decrypt attempt. serial_len 0: no filtering */
  ct = p + 1 + addr_len + 2;
  ct_len = len - (1 + addr_len + 2);

  if (device_emm_u_decrypt(d->ek, ct, ct_len, d->bk) == 0) {
    d->have_bk = 1;
    log_line(TOOL_NAME ": EMM-U decrypted, BK updated");
  } else {
    log_line(TOOL_NAME ": EMM-U decrypt failed");
  }
}

static void handle_emm_g(device_state_t *d, const unsigned char *p, size_t len) {
  unsigned service_id;
  service_key_t *sk;

  if (len < 4 + CRYPTO_EMM_G_LEN || !d->have_bk)
    return;
  service_id = ((unsigned)p[0] << 8) | p[1];

  sk = service_slot(d, service_id, 1);
  if (!sk)
    return;
  if (device_emm_g_decrypt(d->bk, p + 4, sk->sk) == 0) {
    sk->have = 1;
    log_line(TOOL_NAME ": EMM-G decrypted, SK updated for service %04X", service_id);
  } else {
    log_line(TOOL_NAME ": EMM-G decrypt failed for service %04X", service_id);
  }
}

/* EMM-G payload is a fixed size (2+2+60=64); EMM-U's RSA ciphertext makes it larger */
#define EMM_G_PAYLOAD_LEN (4 + CRYPTO_EMM_G_LEN)

void device_on_emm(device_state_t *d, const unsigned char *emm, size_t emm_len) {
  size_t hdr_len = 3;
  const unsigned char *payload;
  size_t payload_len;

  if (emm_len < hdr_len)
    return;
  payload = emm + hdr_len;
  payload_len = emm_len - hdr_len;

  if (payload_len == EMM_G_PAYLOAD_LEN)
    handle_emm_g(d, payload, payload_len);
  else
    handle_emm_u(d, payload, payload_len);
}

/* oscam writes cw[0:8)/[8:16) as independent odd/even hw keys, skipping only an
   all-zero half - never duplicate, or every answer clobbers the other parity's key */
#define SC_SECTION_TID_ECM_ODD 0x81

int device_resolve_cw(device_state_t *d, const unsigned char *ecm, size_t ecm_len, unsigned srvid, unsigned caid, unsigned char cw_out[16]) {
  service_key_t *sk;
  unsigned char cw[16];

  if (d->caid && caid != d->caid)
    return -2; /* permanent: wrong caid, never answerable regardless of EMM state */

  /* section header(3) + CP_CW_COMBINATION's cp_number(2), then the encrypted CW block */
  if (ecm_len < 5 + CRYPTO_CW_ENC_LEN)
    return -1;
  sk = service_slot(d, srvid, 0);
  if (!sk || !sk->have)
    return -1;
  if (device_ecm_decrypt(sk->sk, ecm + 5, d->cw_len, cw) != 0)
    return -1;

  memset(cw_out, 0, 16);
  if (d->cw_len == 16) {
    memcpy(cw_out, cw, 16);
  } else if (ecm[0] == SC_SECTION_TID_ECM_ODD) {
    memcpy(cw_out, cw, (size_t)d->cw_len);
  } else {
    memcpy(cw_out + 8, cw, (size_t)d->cw_len);
  }
  memset(cw, 0, sizeof cw);
  return 0;
}
