/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_CAS_EMMG_SERVER_H
#define DIPITVHEAD_CAS_EMMG_SERVER_H

#include <stddef.h>

typedef struct {
  unsigned port; /* dual-stack (v4+v6) wildcard listener */
} emmg_server_cfg_t;

typedef struct emmg_server emmg_server_t;

emmg_server_t *emmg_server_start(const emmg_server_cfg_t *cfg);
/* stops the accept thread and every worker, joins all of them */
void emmg_server_stop(emmg_server_t *s);

/* actual bound port; useful when cfg.port was 0 (kernel-assigned) */
unsigned emmg_server_port(emmg_server_t *s);

/* dequeues one received EMM datagram (raw bytes, ready to packetize on --cas-emm-pid).
   0 = filled out/len_out, -1 = queue empty */
int emmg_server_dequeue_emm(emmg_server_t *s, unsigned char *out, size_t cap, size_t *len_out);

/* ETSI TS 103 197 clause 6.2: EMMG<->MUX message_type/parameter_type values. wire-format
   internals, exposed (with the builders/parsers below) only so unit tests can exercise them
  with synthetic buffers - cas.c has no business calling these directly. */
#define EMMG_MSG_CHANNEL_SETUP 0x0011
#define EMMG_MSG_CHANNEL_TEST 0x0012
#define EMMG_MSG_CHANNEL_STATUS 0x0013
#define EMMG_MSG_CHANNEL_CLOSE 0x0014
#define EMMG_MSG_CHANNEL_ERROR 0x0015
#define EMMG_MSG_STREAM_SETUP 0x0111
#define EMMG_MSG_STREAM_TEST 0x0112
#define EMMG_MSG_STREAM_STATUS 0x0113
#define EMMG_MSG_STREAM_CLOSE_REQUEST 0x0114
#define EMMG_MSG_STREAM_CLOSE_RESPONSE 0x0115
#define EMMG_MSG_STREAM_ERROR 0x0116
#define EMMG_MSG_STREAM_BW_REQUEST 0x0117
#define EMMG_MSG_STREAM_BW_ALLOCATION 0x0118
#define EMMG_MSG_DATA_PROVISION 0x0211

#define EMMG_P_CLIENT_ID 0x0001
#define EMMG_P_SECTION_TSPKT_FLAG 0x0002
#define EMMG_P_DATA_CHANNEL_ID 0x0003
#define EMMG_P_DATA_STREAM_ID 0x0004
#define EMMG_P_DATAGRAM 0x0005
#define EMMG_P_BANDWIDTH 0x0006
#define EMMG_P_DATA_TYPE 0x0007
#define EMMG_P_DATA_ID 0x0008
#define EMMG_P_ERROR_STATUS 0x7000

#define EMMG_MAX_DATAGRAM_LEN 4096

/* all builders: 0 on overflow, else total frame bytes */
size_t emmg_build_channel_status(unsigned char *out, size_t cap, unsigned char version, unsigned client_id, unsigned data_channel_id);
size_t emmg_build_stream_status(unsigned char *out, size_t cap, unsigned char version, unsigned client_id, unsigned data_channel_id, unsigned data_stream_id, unsigned data_id, unsigned data_type);
size_t emmg_build_stream_close_response(unsigned char *out, size_t cap, unsigned char version, unsigned client_id, unsigned data_channel_id, unsigned data_stream_id);
/* have_bandwidth: 0 = omit the bandwidth field ("allocated bandwidth not known" per spec) */
size_t emmg_build_stream_bw_allocation(unsigned char *out, size_t cap, unsigned char version, unsigned client_id, unsigned data_channel_id, unsigned data_stream_id, int have_bandwidth, unsigned bandwidth_kbps);

/* iterates every "datagram" (0x0005) TLV in a data_provision payload. cb returns nonzero to stop early. returns count handed to cb, or -1 = malformed */
typedef void (*emmg_datagram_cb)(const unsigned char *data, unsigned short len, void *user);
int emmg_extract_datagrams(const unsigned char *body, size_t body_len, emmg_datagram_cb cb, void *user);

#endif
