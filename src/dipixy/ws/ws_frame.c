/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "ws_frame.h"

#include <stdlib.h>
#include <string.h>

static int buf_reserve(uint8_t **buf, size_t *cap, size_t need) {
  uint8_t *p;
  size_t ncap;
  if (need <= *cap)
    return 0;
  ncap = *cap ? *cap * 2 : 256;
  if (ncap < need)
    ncap = need;
  p = realloc(*buf, ncap);
  if (!p)
    return -1;
  *buf = p;
  *cap = ncap;
  return 0;
}

void ws_parser_init(ws_parser_t *p) {
  memset(p, 0, sizeof *p);
  buf_reserve(&p->buf, &p->cap, 256);
  buf_reserve(&p->msg, &p->msg_cap, 256);
}

void ws_parser_free(ws_parser_t *p) {
  free(p->buf);
  free(p->msg);
  memset(p, 0, sizeof *p);
}

int ws_parser_feed(ws_parser_t *p, const uint8_t *data, size_t len) {
  if (buf_reserve(&p->buf, &p->cap, p->len + len))
    return -1;
  memcpy(p->buf + p->len, data, len);
  p->len += len;
  return 0;
}

static void consume(ws_parser_t *p, size_t n) {
  memmove(p->buf, p->buf + n, p->len - n);
  p->len -= n;
}

int ws_parser_next(ws_parser_t *p, int *opcode, const uint8_t **payload, size_t *payload_len) {
  for (;;) {
    size_t need, hdr, i;
    int fin, op, masked;
    uint64_t plen64;
    const uint8_t *mask, *body;

    if (p->len < 2)
      return 0;
    fin = (p->buf[0] & 0x80) != 0;
    op = p->buf[0] & 0x0f;
    masked = (p->buf[1] & 0x80) != 0;
    plen64 = p->buf[1] & 0x7f;
    hdr = 2;

    if (plen64 == 126) {
      if (p->len < 4)
        return 0;
      plen64 = ((uint64_t)p->buf[2] << 8) | p->buf[3];
      hdr = 4;
    } else if (plen64 == 127) {
      if (p->len < 10)
        return 0;
      plen64 = 0;
      for (i = 0; i < 8; i++)
        plen64 = (plen64 << 8) | p->buf[2 + i];
      hdr = 10;
    }

    if (!masked)
      return -1; /* client frames must be masked, RFC6455 SS5.1 */
    if ((op == WS_OP_CLOSE || op == WS_OP_PING || op == WS_OP_PONG) && (plen64 > 125 || !fin))
      return -1;

    need = hdr + 4 + (size_t)plen64;
    if (p->len < need)
      return 0;

    mask = p->buf + hdr;
    body = p->buf + hdr + 4;

    if (op == WS_OP_CLOSE || op == WS_OP_PING || op == WS_OP_PONG) {
      for (i = 0; i < (size_t)plen64; i++)
        p->ctrl_payload[i] = body[i] ^ mask[i % 4];
      p->ctrl_payload_len = (size_t)plen64;
      *opcode = op;
      *payload = p->ctrl_payload;
      *payload_len = p->ctrl_payload_len;
      consume(p, need);
      return 1;
    }

    if (op == WS_OP_TEXT || op == WS_OP_BINARY) {
      p->msg_len = 0;
      p->msg_opcode = op;
      p->have_msg_opcode = 1;
    } else if (op == WS_OP_CONTINUATION) {
      if (!p->have_msg_opcode)
        return -1;
    } else {
      return -1; /* unknown opcode */
    }

    if (buf_reserve(&p->msg, &p->msg_cap, p->msg_len + (size_t)plen64))
      return -1;
    for (i = 0; i < (size_t)plen64; i++)
      p->msg[p->msg_len + i] = body[i] ^ mask[i % 4];
    p->msg_len += (size_t)plen64;
    consume(p, need);

    if (fin) {
      *opcode = p->msg_opcode;
      *payload = p->msg;
      *payload_len = p->msg_len;
      p->have_msg_opcode = 0;
      return 1;
    }
  }
}

size_t ws_frame_hdr_len(size_t payload_len) {
  if (payload_len <= 125)
    return 2;
  if (payload_len <= 0xffff)
    return 4;
  return 10;
}

void ws_frame_encode(uint8_t *out, int opcode, const void *payload, size_t payload_len) {
  size_t hdr = ws_frame_hdr_len(payload_len);

  out[0] = 0x80 | (uint8_t)opcode; /* FIN=1: server frames never fragment */
  if (payload_len <= 125) {
    out[1] = (uint8_t)payload_len;
  } else if (payload_len <= 0xffff) {
    out[1] = 126;
    out[2] = (uint8_t)(payload_len >> 8);
    out[3] = (uint8_t)payload_len;
  } else {
    out[1] = 127;
    for (size_t i = 0; i < 8; i++)
      out[2 + i] = (uint8_t)((uint64_t)payload_len >> (56 - 8 * i));
  }
  if (payload_len)
    memcpy(out + hdr, payload, payload_len);
}

uint8_t *ws_build_frame(int opcode, const void *payload, size_t payload_len, size_t *out_len) {
  size_t hdr = ws_frame_hdr_len(payload_len);
  uint8_t *out = malloc(hdr + payload_len);
  if (!out)
    return NULL;
  ws_frame_encode(out, opcode, payload, payload_len);
  *out_len = hdr + payload_len;
  return out;
}
