/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_RTMP_PRIV_H
#define DVBIPITOOLS_LIB_NET_RTMP_PRIV_H

#include <stddef.h>
#include <stdint.h>

#include "chunk.h"
#include "handshake.h"
#include "rtmp.h"

#define RTMP_CID_PROTOCOL 2
#define RTMP_CID_INVOKE 3
#define RTMP_CID_AUDIO 4
#define RTMP_CID_VIDEO 5

#define RTMP_N_CHAN 8
#define RTMP_MAX_HEADER 18 /* 3B basic + 11B message + 4B extended timestamp */
#define RTMP_TRANSACTION_CONNECT 1
#define RTMP_TRANSACTION_CREATE_STREAM 2

#define RTMP_TYPE_SET_CHUNK_SIZE 1
#define RTMP_TYPE_AUDIO 8
#define RTMP_TYPE_VIDEO 9
#define RTMP_TYPE_DATA 18
#define RTMP_TYPE_INVOKE 20

typedef struct {
  uint32_t cid;
  rtmp_chunk_header_t header;
  uint32_t clock;
  int used;
} rtmp_out_chan_t;

typedef struct {
  uint32_t cid;
  rtmp_chunk_header_t header;
  uint32_t clock;
  unsigned char *payload;
  size_t capacity;
  size_t bytes;
  int used;
} rtmp_in_chan_t;

typedef enum {
  RTMP_PARSE_INIT = 0,
  RTMP_PARSE_BASIC_HEADER,
  RTMP_PARSE_MESSAGE_HEADER,
  RTMP_PARSE_EXTENDED_TIMESTAMP,
  RTMP_PARSE_PAYLOAD
} rtmp_parse_state_t;

typedef struct {
  unsigned char buffer[RTMP_MAX_HEADER];
  uint32_t basic_bytes;
  uint32_t bytes;
  rtmp_parse_state_t state;
  rtmp_in_chan_t *pkt;
} rtmp_parser_t;

typedef enum {
  RTMP_ST_UNINIT = 0,
  RTMP_ST_WAIT_S0,
  RTMP_ST_WAIT_S1,
  RTMP_ST_WAIT_S2,
  RTMP_ST_WAIT_CONNECT_RESULT,
  RTMP_ST_WAIT_CREATE_STREAM_RESULT,
  RTMP_ST_READY,
  RTMP_ST_FAILED
} rtmp_client_state_t;

struct rtmp {
  rtmp_client_state_t state;
  uint32_t in_chunk_size, out_chunk_size;
  uint32_t stream_id;

  char app[128], tcurl[512], stream_name[256];
  char user[128], password[128];
  char auth_query[400]; /* appended to connect app once auth needed */
  int auth_tried;       /* credentialed retry sent, no more retries */

  rtmp_out_chan_t out_chan[RTMP_N_CHAN];
  rtmp_in_chan_t in_chan[RTMP_N_CHAN];
  rtmp_parser_t parser;

  unsigned char s1[RTMP_HANDSHAKE_SIZE];
  size_t hs_bytes;

  rtmp_write_cb write_cb;
  rtmp_ready_cb ready_cb;
  rtmp_error_cb error_cb;
  void *cb_ctx;

  int err;
};

int rtmp_session_write_message(struct rtmp *r, uint32_t cid, unsigned char type, uint32_t stream_id, uint32_t timestamp, const unsigned char *payload, size_t len);
int rtmp_session_feed(struct rtmp *r, const unsigned char *data, size_t bytes);

int rtmp_command_connect(struct rtmp *r);
int rtmp_command_release_stream(struct rtmp *r);
int rtmp_command_fcpublish(struct rtmp *r);
int rtmp_command_create_stream(struct rtmp *r);
int rtmp_command_publish(struct rtmp *r);
void rtmp_command_on_invoke(struct rtmp *r, const unsigned char *payload, size_t len);

void rtmp_on_message(struct rtmp *r, unsigned char type, uint32_t timestamp, const unsigned char *payload, size_t len);

#endif
