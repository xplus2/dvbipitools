/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_CAS_DEVICE_STATE_CORE_H
#define LIB_CAS_DEVICE_STATE_CORE_H

#include <stddef.h>

#include "device_crypto.h"

#define DEVICE_MAX_SERVICES 32
#define DEVICE_SERIAL_MAX 256 /* addr_len is one wire byte, max 255 + nul */

typedef struct {
  unsigned service_id;
  unsigned char sk[CRYPTO_KEY_LEN];
  int have;
} service_key_t;

/* caller embeds by value in its own device_state, alongside its own tool-specific fields */
typedef struct {
  EVP_PKEY *ek;
  char serial[DEVICE_SERIAL_MAX];
  size_t serial_len; /* 0 = no EMM-U address filtering */
  unsigned char bk[CRYPTO_KEY_LEN];
  int have_bk;
  service_key_t services[DEVICE_MAX_SERVICES];
  size_t service_count;
} device_core_t;

/* loads device key, copies serial (NULL/"" ok, means no EMM-U filtering). 0 ok, -1 err */
int device_core_init(device_core_t *core, const char *key_path, const char *serial);

void device_core_release(device_core_t *core);

/* finds service_id's key slot. create: adds one if not found and cache not full. NULL if not found/full */
service_key_t *device_core_service_slot(device_core_t *core, unsigned service_id, int create);

/* one EMM section (table_id+length+payload). EMM-U updates BK, EMM-G updates SK cache for its dvb_service_id.
   log_prefix prepended to progress log lines. 1 if state changed, 0 otherwise */
int device_core_on_emm(device_core_t *core, const unsigned char *emm, size_t emm_len, const char *log_prefix);

#endif
