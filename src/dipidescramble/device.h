/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_DEVICE_H
#define DIPIDESCRAMBLE_DEVICE_H

#include <stddef.h>

#include "ecm_profile.h"

typedef struct device_state device_state_t;

/* serial: matched against EMM-U addressing. No cw_len here.
   only device_resolve_cw() needs it, read from PMT. profile: ecm_profile.set == 0 keeps AES-256-ECB.
   copied by value, caller's copy not retained. max_services: 0 = default (32), else
   1..DEVICE_MAX_SERVICES_CEILING. NULL on err */
device_state_t *device_state_new(const char *key_path, const char *serial, const ecm_profile_t *profile, size_t max_services);
void device_state_free(device_state_t *d);

/* one EMM section (table_id + length + payload). EMM-U updates BK, EMM-G updates SK cache for its dvb_service_id.
   1 if state changed, 0 otherwise (not ours, decrypt failed, malformed, upstream bug, ... ) */
int device_on_emm(device_state_t *d, const unsigned char *emm, size_t emm_len);

/* resolve one ECM section for given service into a CW. cw_len: 8 (CSA2) or 16 (CISSA+CSA3), from PMT's
   scrambling_descriptor mode byte.
   ecm_pid: this stream's ECM PID, used as ecm_profile's ecm_id fallback (Simulcrypt-blind receiver has no other way to learn it)
   when profile.ecm_id is unset.
   profiles with cw_count > 1 (lead-CW packing): only 1st (current) combo applied. lead/next combo(s) still fully decrypted and integrity-checked,
   just not pre-fetched.
   0 ok (cw_out[16] filled; CSA2's 8 bytes duplicated into both halves), -1 no SK yet, bad ECM, bad cw_len, or (profile) decrypt/integrity failure */
int device_resolve_cw(device_state_t *d, const unsigned char *ecm, size_t ecm_len, unsigned srvid, int cw_len, unsigned ecm_pid, unsigned char cw_out[16]);

#endif
