/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_WS_FRAME_H
#define DIPIXY_WS_FRAME_H

#include <stddef.h>
#include <stdint.h>

/* RFC 6455 SS5.2 opcodes */
typedef enum {
  WS_OP_CONTINUATION = 0x0,
  WS_OP_TEXT = 0x1,
  WS_OP_BINARY = 0x2,
  WS_OP_CLOSE = 0x8,
  WS_OP_PING = 0x9,
  WS_OP_PONG = 0xA
} ws_opcode_t;

/* incremental decoder: feed raw bytes, drain complete messages. masked client
   frames only, reassembles fragments */
typedef struct {
  uint8_t *buf;
  size_t len, cap;
  uint8_t *msg;
  size_t msg_len, msg_cap;
  int msg_opcode;
  int have_msg_opcode;
  uint8_t ctrl_payload[125]; /* close/ping/pong: never fragmented, RFC6455 SS5.5 caps at 125 */
  size_t ctrl_payload_len;
} ws_parser_t;

void ws_parser_init(ws_parser_t *p);
void ws_parser_free(ws_parser_t *p);

/* 0 ok, -1 protocol error */
int ws_parser_feed(ws_parser_t *p, const uint8_t *data, size_t len);

/* pops one complete message/control frame, payload valid until next feed/next call. 1: got one, 0: none pending */
int ws_parser_next(ws_parser_t *p, int *opcode, const uint8_t **payload, size_t *payload_len);

/* server->client frame, always unmasked. malloc'd, *out_len set. NULL on OOM */
uint8_t *ws_build_frame(int opcode, const void *payload, size_t payload_len, size_t *out_len);

size_t ws_frame_hdr_len(size_t payload_len);
void ws_frame_encode(uint8_t *out, int opcode, const void *payload, size_t payload_len);

#endif
