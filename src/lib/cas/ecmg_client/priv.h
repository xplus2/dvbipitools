/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_CAS_ECMG_CLIENT_PRIV_H
#define DVBIPITOOLS_LIB_CAS_ECMG_CLIENT_PRIV_H

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>

#include "../simulcrypt_msg.h"
#include "ecmg_client.h"

#define ECMG_HANDSHAKE_TIMEOUT_MS 3000
#define ECMG_POLL_INTERVAL_MS 150
#define ECMG_RECONNECT_BACKOFF_MIN_MS 500
#define ECMG_RECONNECT_BACKOFF_MAX_MS 30000

struct ecmg_client {
  ecmg_client_cfg_t cfg;
  size_t cw_len;
  cwenc_ctx_t cwenc_ctx;

  const atomic_ulong *packet_counter;
  unsigned long packets_per_cp;
  unsigned long lookahead_margin_packets;

  pthread_t thread;
  atomic_int stop;

  atomic_int connected; /* 1 while a CW_provision/ECM_response cycle is live */
  atomic_ulong cw_published_at; /* packet_counter at last CP provision */

  pthread_mutex_t ecm_lock;
  unsigned char ecm_slot[2][SIMULCRYPT_MAX_PAYLOAD]; /* indexed by parity, ETR 289 */
  size_t ecm_slot_len[2];
  atomic_ulong ecm_epoch; /* monotonic, never reset: freshness signal only, not a parity source */
  atomic_int last_parity; /* latest publish's parity, direct snapshot, reconnect-safe */

  atomic_uint ecm_rep_period_ms;

  atomic_ulong cryptoperiod_transitions_total;
  atomic_ulong ecm_total;
  atomic_ulong ecm_errors_total;
};

/* ecmg_client.c: checked everywhere a network wait could otherwise block shutdown past its own timeout */
int ecmg_stopping(const ecmg_client_t *c);

/* connect.c: dials, negotiates protocol_version, opens channel+1 stream.
   0 ok (fills lead_cw/cw_per_msg/max_comp_time_ms, fd left open), -1 err (fd closed) */
int connect_and_setup(ecmg_client_t *c, int *out_fd, unsigned char *out_version, unsigned *out_lead_cw, unsigned *out_cw_per_msg, unsigned *out_max_comp_time_ms);
/* connect.c: also used by run.c's steady-state loop */
int wait_for_message(ecmg_client_t *c, simulcrypt_reader_t *rd, int fd, int total_timeout_ms, simulcrypt_hdr_t *hdr, const unsigned char **payload);

/* run.c: thread entry point, started by ecmg_client_start() */
void *ecmg_client_main(void *arg);

#endif
