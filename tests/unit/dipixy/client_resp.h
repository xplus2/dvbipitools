/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_TEST_CLIENT_RESP_H
#define DIPIXY_TEST_CLIENT_RESP_H

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RESP_HDR_MAX 8
#define RESP_BODY_MAX 4096

typedef struct {
  int64_t sid;
  int status;
  int closed;
  int ended;
  uint64_t close_code;
  char body[RESP_BODY_MAX];
  size_t body_len;
  size_t total_bytes;
  uint32_t checksum;
  char hdr_name[RESP_HDR_MAX][32];
  char hdr_value[RESP_HDR_MAX][128];
  int hdr_count;
} client_resp_t;

static inline void client_resp_header(client_resp_t *st, const uint8_t *name, size_t nl, const uint8_t *val, size_t vl) {
  if (nl == 7 && !memcmp(name, ":status", 7)) {
    char tmp[16] = {0};

    memcpy(tmp, val, vl < sizeof tmp - 1 ? vl : sizeof tmp - 1);
    st->status = atoi(tmp);
  }
  if (st->hdr_count < RESP_HDR_MAX && nl < sizeof st->hdr_name[0] && vl < sizeof st->hdr_value[0]) {
    memcpy(st->hdr_name[st->hdr_count], name, nl);
    st->hdr_name[st->hdr_count][nl] = '\0';
    memcpy(st->hdr_value[st->hdr_count], val, vl);
    st->hdr_value[st->hdr_count][vl] = '\0';
    st->hdr_count++;
  }
}

static inline void client_resp_data(client_resp_t *st, const uint8_t *data, size_t len) {
  if (st->body_len + len <= sizeof st->body) {
    memcpy(st->body + st->body_len, data, len);
    st->body_len += len;
  }
  for (size_t i = 0; i < len; i++) st->checksum = st->checksum * 31u + data[i];
  st->total_bytes += len;
}

static inline const char *client_resp_header_value(const client_resp_t *st, const char *name) {
  for (int i = 0; i < st->hdr_count; i++) {
    if (!strcmp(st->hdr_name[i], name)) return st->hdr_value[i];
  }
  return NULL;
}

#define WS_MASK_BYTES 4

static inline size_t client_ws_masked_frame(uint8_t *out, int opcode, const uint8_t *payload, size_t len) {
  static const uint8_t mask[WS_MASK_BYTES] = {0x11, 0x22, 0x33, 0x44};

  ck_assert_uint_lt(len, 126u);
  out[0] = (uint8_t)(0x80 | opcode);
  out[1] = (uint8_t)(0x80 | len);
  memcpy(out + 2, mask, WS_MASK_BYTES);
  for (size_t i = 0; i < len; i++) out[2 + WS_MASK_BYTES + i] = payload[i] ^ mask[i % WS_MASK_BYTES];
  return 2 + WS_MASK_BYTES + len;
}

#endif
