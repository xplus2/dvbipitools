/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPICAM378_DEVICE_H
#define DIPICAM378_DEVICE_H

#include <stddef.h>

typedef struct device_state device_state_t;

/* cw_len: 8 (CSA2) or 16 (CISSA). serial: NULL/"" = no EMM-U filtering.
   caid: 0 = no filtering (resolve_cw never returns -2) else ret CAID only. NULL on err */
device_state_t *device_state_new(const char *key_path, int cw_len, const char *serial, unsigned caid);
void device_state_free(device_state_t *d);

/* one EMM section, as captured. EMM-U updates BK, EMM-G updates SK cache for its dvb_service_id */
void device_on_emm(device_state_t *d, const unsigned char *emm, size_t emm_len);

/* resolve one ECM section for given service into CW.
    0 ok: cw_out[16] filled
      CISSA: fills all 16
      CSA2: 8B CW in the half matching the ECM's table_id parity (0x81 odd -> [0:8), else even -> [8:16)), other half zeroed
   -1 transient: no SK/bad ECM, stay silent, peer keeps retrying.
   -2 permanent: caid configured and mismatched, peer may stop asking */
int device_resolve_cw(device_state_t *d, const unsigned char *ecm, size_t ecm_len, unsigned srvid, unsigned caid, unsigned char cw_out[16]);

#endif
