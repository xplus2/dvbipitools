/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/helper/log.h"

#include "device_state_core.h"

int device_core_init(device_core_t *core, const char *key_path, const char *serial, size_t max_services) {
  size_t serial_len = serial ? strlen(serial) : 0;
  if (serial_len >= sizeof core->serial)
    return -1;
  memset(core, 0, sizeof *core);
  if (device_key_load(key_path, &core->ek) != 0)
    return -1;
  if (serial_len)
    memcpy(core->serial, serial, serial_len);
  core->serial_len = serial_len;
  core->max_services = max_services ? max_services : 32;
  if (core->max_services > DEVICE_MAX_SERVICES_CEILING)
    core->max_services = DEVICE_MAX_SERVICES_CEILING;
  return 0;
}

void device_core_release(device_core_t *core) { EVP_PKEY_free(core->ek); }

service_key_t *device_core_service_slot(device_core_t *core, unsigned service_id, int create) {
  for (size_t i = 0; i < core->service_count; i++)
    if (core->services[i].service_id == service_id)
      return &core->services[i];
  if (!create || core->service_count >= core->max_services)
    return NULL;
  core->services[core->service_count].service_id = service_id;
  core->services[core->service_count].have = 0;
  return &core->services[core->service_count++];
}

static int handle_emm_u(device_core_t *core, const unsigned char *p, size_t len, const char *log_prefix) {
  unsigned addr_len;
  const unsigned char *ct;
  size_t ct_len;

  if (len < 3)
    return 0;
  addr_len = p[0];
  if (len < 1 + addr_len + 2)
    return 0;
  if (core->serial_len && (addr_len != core->serial_len || memcmp(p + 1, core->serial, addr_len) != 0))
    return 0; /* not ours, skip. serial_len 0: no filtering */
  ct = p + 1 + addr_len + 2;
  ct_len = len - (1 + addr_len + 2);

  if (device_emm_u_decrypt(core->ek, ct, ct_len, core->bk) == 0) {
    core->have_bk = 1;
    log_line("%sEMM-U decrypted, BK updated", log_prefix);
    return 1;
  }
  log_line("%sEMM-U decrypt failed", log_prefix);
  return 0;
}

static int handle_emm_g(device_core_t *core, const unsigned char *p, size_t len, const char *log_prefix) {
  unsigned service_id;
  service_key_t *sk;

  if (len < 4 + CRYPTO_EMM_G_LEN || !core->have_bk)
    return 0;
  service_id = ((unsigned)p[0] << 8) | p[1];

  sk = device_core_service_slot(core, service_id, 1);
  if (!sk) {
    log_line("%sservice key cache full (%zu services), dropping EMM-G for service %04X", log_prefix, core->max_services, service_id);
    return 0;
  }
  if (device_emm_g_decrypt(core->bk, p + 4, sk->sk) == 0) {
    sk->have = 1;
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

  if (emm_len < hdr_len)
    return 0;
  payload = emm + hdr_len;
  payload_len = emm_len - hdr_len;

  if (payload_len == EMM_G_PAYLOAD_LEN)
    return handle_emm_g(core, payload, payload_len, log_prefix);
  return handle_emm_u(core, payload, payload_len, log_prefix);
}
