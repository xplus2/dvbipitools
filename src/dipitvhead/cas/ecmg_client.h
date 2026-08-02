/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_CAS_ECMG_CLIENT_H
#define DIPITVHEAD_CAS_ECMG_CLIENT_H

#include <stdatomic.h>
#include <stddef.h>

#include "lib/scrambler/scrambler.h"

typedef enum { ECMG_RESILIENCE_FROZEN, ECMG_RESILIENCE_CYCLING, ECMG_RESILIENCE_UNSCRAMBLED } ecmg_resilience_t;

typedef struct {
  const char *host;
  unsigned port;
  unsigned char version_min; /* 2 or 3 */
  unsigned char version_max; /* 2 or 3, >= version_min; first attempt uses max, falls back to min */
  unsigned super_cas_id;     /* 32 bit */
  unsigned ecm_id;           /* 16 bit */
  unsigned cp_duration_ms;   /* sent as nominal_CP_duration (n x 100ms) in stream_setup */
  scramble_algo_t algo;      /* picks the wire CW length via scrambler_cw_len() */
  ecmg_resilience_t resilience;
} ecmg_client_cfg_t;

typedef struct ecmg_client ecmg_client_t;

/* packet_counter: whole-stream output packet count, incremented by caller's hot path.
   packets_per_cp/lookahead_margin_packets: crypto-period cadence in packets, caller's thing */
ecmg_client_t *ecmg_client_start(const ecmg_client_cfg_t *cfg, const atomic_ulong *packet_counter, unsigned long packets_per_cp, unsigned long lookahead_margin_packets);
/* joins the thread */
void ecmg_client_stop(ecmg_client_t *c);

/* scrambling key for slot (0/1, picked by caller as cp_number & 1). 0 = filled cw_out/cw_len_out, -1 = not provisioned yet */
int ecmg_client_get_cw(ecmg_client_t *c, int slot, unsigned char *cw_out, size_t cw_cap, size_t *cw_len_out);
/* bumps whenever a slot is (re)written; lets a poller notice a fresh key without locking */
unsigned long ecmg_client_cw_epoch(ecmg_client_t *c);

/* latest ECM_datagram (MPEG-2 section, ready to packetize) for --cas-ecm-pid. 0 = filled out/len_out, -1 = none yet */
int ecmg_client_get_ecm(ecmg_client_t *c, unsigned char *out, size_t cap, size_t *len_out);
unsigned long ecmg_client_ecm_epoch(ecmg_client_t *c);

/* ECM_rep_period from channel_status: how often the ECMG wants the current ECM section
 * repeated on the output. 0 = channel not established yet. */
unsigned ecmg_client_ecm_rep_period_ms(ecmg_client_t *c);

/* --cas-resilience=unscrambled gate: 0 = the cached CW must not be used to scramble anymore.
   always 1 for frozen/cycling - they keep using known CW material indefinitely. */
int ecmg_client_cw_valid(ecmg_client_t *c);

/* which cw_slot (0/1) cas.c should scramble with right now. frozen: always the last published
   parity. cycling, while disconnected: keeps alternating on the normal crypto-period schedule
   between the two last-known CWs, using the PCR-derived clock rather than waiting on the ECMG. */
int ecmg_client_target_parity(ecmg_client_t *c);

/* pure logic behind the two accessors above, exposed only for unit tests to exercise directly
   with synthetic values - cas.c has no business calling these, use the accessors instead */
int ecmg_cw_valid_calc(ecmg_resilience_t resilience, int connected, unsigned long packets_per_cp, unsigned long cur, unsigned long published_at);
int ecmg_target_parity_calc(ecmg_resilience_t resilience, int connected, unsigned long packets_per_cp, unsigned long cur, unsigned long published_at, unsigned long epoch);

/* ETSI TS 103 197 clause 5: ECMG<->SCS message_type/parameter_type values.
   format porn, exposed (with builders/parsers) for unit test synthetic buffers.
   cas.c has no business calling anything here directly */
#define ECMG_MSG_CHANNEL_SETUP 0x0001
#define ECMG_MSG_CHANNEL_STATUS 0x0003
#define ECMG_MSG_CHANNEL_CLOSE 0x0004
#define ECMG_MSG_CHANNEL_ERROR 0x0005
#define ECMG_MSG_STREAM_SETUP 0x0101
#define ECMG_MSG_STREAM_STATUS 0x0103
#define ECMG_MSG_STREAM_ERROR 0x0106
#define ECMG_MSG_CW_PROVISION 0x0201
#define ECMG_MSG_ECM_RESPONSE 0x0202

#define ECMG_P_SUPER_CAS_ID 0x0001
#define ECMG_P_ECM_REP_PERIOD 0x0007
#define ECMG_P_MIN_CP_DURATION 0x0009
#define ECMG_P_LEAD_CW 0x000A
#define ECMG_P_CW_PER_MSG 0x000B
#define ECMG_P_MAX_COMP_TIME 0x000C
#define ECMG_P_ECM_CHANNEL_ID 0x000E
#define ECMG_P_ECM_STREAM_ID 0x000F
#define ECMG_P_NOMINAL_CP_DURATION 0x0010
#define ECMG_P_CP_NUMBER 0x0012
#define ECMG_P_CP_CW_COMBINATION 0x0014
#define ECMG_P_ECM_DATAGRAM 0x0015
#define ECMG_P_ECM_ID 0x0019
#define ECMG_P_ERROR_STATUS 0x7000

#define ECMG_ERR_UNSUPPORTED_PROTOCOL_VERSION 0x0002

#define ECMG_CHANNEL_ID 1
#define ECMG_STREAM_ID 1

#define ECMG_MAX_CW_PER_MSG 16
#define ECMG_MAX_CW_LEN 16
#define ECMG_CW_HIST 8 /* hist[] array size expected by ecmg_build_cw_provision() */

typedef struct {
  int valid;
  unsigned short cp_number;
  unsigned char cw[ECMG_MAX_CW_LEN];
} cw_hist_entry_t;

size_t ecmg_build_channel_setup(unsigned char *out, size_t cap, unsigned char version, unsigned super_cas_id);
size_t ecmg_build_stream_setup(unsigned char *out, size_t cap, unsigned char version, unsigned ecm_id, unsigned nominal_cp_100ms);
size_t ecmg_build_cw_provision(unsigned char *out, size_t cap, unsigned char version, unsigned short cp_number, cw_hist_entry_t *hist, size_t cw_len, unsigned lead_cw, unsigned cw_per_msg);

int ecmg_find_error_status(const unsigned char *body, size_t body_len, unsigned short *out);
/* 0 ok (fills all five), -1 malformed or cw_per_msg missing/out of range */
int ecmg_parse_channel_status(const unsigned char *body, size_t body_len, unsigned *out_lead_cw, unsigned *out_cw_per_msg, unsigned *out_max_comp_time_ms, unsigned *out_min_cp_100ms, unsigned *out_ecm_rep_period_ms);

#endif
