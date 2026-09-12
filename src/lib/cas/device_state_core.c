/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/helper/log.h"
#include "device_state_core.h"

int device_core_init(device_core_t *core, const char *key_path, const char *serial, size_t max_services) {
  size_t serial_len = serial ? strlen(serial) : 0;
  if (serial_len >= sizeof core->serial) return -1;
  memset(core, 0, sizeof *core);
  if (device_key_load(key_path, &core->ek) != 0) return -1;
  if (serial_len) memcpy(core->serial, serial, serial_len);
  core->serial_len = serial_len;
  core->max_services = max_services ? max_services : 32;
  if (core->max_services > DEVICE_MAX_SERVICES_CEILING) core->max_services = DEVICE_MAX_SERVICES_CEILING;
  pthread_mutex_init(&core->lock, NULL);
  return 0;
}

void device_core_release(device_core_t *core) {
  pthread_mutex_destroy(&core->lock);
  EVP_PKEY_free(core->ek);
}

void *device_core_alloc(size_t state_size, const char *key_path, const char *serial, size_t max_services) {
  void *d = calloc(1, state_size);
  if (!d) return NULL;
  if (device_core_init((device_core_t *)d, key_path, serial, max_services) != 0) {
    free(d);
    return NULL;
  }
  return d;
}

void device_core_free_state(void *state) {
  if (!state) return;
  device_core_release((device_core_t *)state);
  free(state);
}

void device_core_lock(device_core_t *core) { pthread_mutex_lock(&core->lock); }

void device_core_unlock(device_core_t *core) { pthread_mutex_unlock(&core->lock); }

service_key_t *device_core_service_slot_locked(device_core_t *core, unsigned service_id, int create) {
  for (size_t i = 0; i < core->service_count; i++) if (core->services[i].service_id == service_id) return &core->services[i];
  if (!create || core->service_count >= core->max_services) return NULL;
  core->services[core->service_count].service_id = service_id;
  core->services[core->service_count].have = 0;
  return &core->services[core->service_count++];
}

unsigned device_core_services_active(device_core_t *core) {
  unsigned n = 0;
  device_core_lock(core);
  for (size_t i = 0; i < core->service_count; i++) if (core->services[i].have) n++;
  device_core_unlock(core);
  return n;
}

static int handle_emm_u(device_core_t *core, const unsigned char *p, size_t len, const char *log_prefix) {
  unsigned addr_len;
  const unsigned char *ct;
  size_t ct_len;
  unsigned char bk[CRYPTO_KEY_LEN];

  if (len < 3) return 0;
  addr_len = p[0];
  if (len < 1 + addr_len + 2) return 0;
  if (core->serial_len && (addr_len != core->serial_len || memcmp(p + 1, core->serial, addr_len) != 0))
    return 0; /* not ours, skip. serial_len 0: no filtering */
  ct = p + 1 + addr_len + 2;
  ct_len = len - (1 + addr_len + 2);
  if (device_emm_u_decrypt(core->ek, ct, ct_len, bk) == 0) {
    device_core_lock(core);
    memcpy(core->bk, bk, sizeof core->bk);
    core->have_bk = 1;
    device_core_unlock(core);
    log_line("%sEMM-U decrypted, BK updated", log_prefix);
    return 1;
  }
  log_line("%sEMM-U decrypt failed", log_prefix);
  return 0;
}

static int handle_emm_g(device_core_t *core, const unsigned char *p, size_t len, const char *log_prefix) {
  unsigned service_id;
  service_key_t *sk;
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char sk_buf[CRYPTO_KEY_LEN];

  if (len < 4 + CRYPTO_EMM_G_LEN) return 0;
  service_id = ((unsigned)p[0] << 8) | p[1];

  device_core_lock(core);
  if (!core->have_bk) {
    device_core_unlock(core);
    return 0;
  }
  sk = device_core_service_slot_locked(core, service_id, 1);
  if (!sk) {
    device_core_unlock(core);
    log_line("%sservice key cache full (%zu services), dropping EMM-G for service %04X", log_prefix, core->max_services, service_id);
    return 0;
  }
  memcpy(bk, core->bk, sizeof bk);
  device_core_unlock(core);

  if (device_emm_g_decrypt(bk, p + 4, sk_buf) == 0) {
    device_core_lock(core);
    memcpy(sk->sk, sk_buf, sizeof sk->sk);
    sk->have = 1;
    device_core_unlock(core);
    log_line("%sEMM-G decrypted, SK updated for service %04X", log_prefix, service_id);
    return 1;
  }
  log_line("%sEMM-G decrypt failed for service %04X", log_prefix, service_id);
  return 0;
}

/* EMM-G payload fixed size (2+2+60=64), EMM-U's RSA ciphertext larger */
#define EMM_G_PAYLOAD_LEN (4 + CRYPTO_EMM_G_LEN)

int device_core_on_emm(device_core_t *core, const unsigned char *emm, size_t emm_len, const char *log_prefix) {
  size_t hdr_len = 3;
  const unsigned char *payload;
  size_t payload_len;

  if (emm_len < hdr_len) return 0;
  payload = emm + hdr_len;
  payload_len = emm_len - hdr_len;
  if (payload_len == EMM_G_PAYLOAD_LEN) return handle_emm_g(core, payload, payload_len, log_prefix);
  return handle_emm_u(core, payload, payload_len, log_prefix);
}

int device_core_copy_service_key(device_core_t *core, unsigned service_id, int allow_sole_fallback, unsigned char sk_copy[CRYPTO_KEY_LEN]) {
  const service_key_t *sk;
  int have;
  device_core_lock(core);
  sk = device_core_service_slot_locked(core, service_id, 0);
  if (!sk && allow_sole_fallback && core->service_count == 1) sk = &core->services[0];
  have = sk && sk->have;
  if (have) memcpy(sk_copy, sk->sk, CRYPTO_KEY_LEN);
  device_core_unlock(core);
  return have ? 0 : -1;
}
