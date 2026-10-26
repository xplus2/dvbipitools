/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_TESTS_UNIT_FX_PROTO_H
#define DVBIPITOOLS_TESTS_UNIT_FX_PROTO_H

#include <check.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "lib/demux/rtcp.h"
#include "lib/cas/simulcrypt_msg.h"
#include "lib/net/rtmp/chunk.h"
#include "lib/net/rtmp/priv.h"
#include "lib/mux/amf.h"
#include "dipifccret/channel/channel.h"

static inline size_t build_chunk(unsigned char *out, uint32_t cid, unsigned char type, uint32_t stream_id, const unsigned char *payload, size_t len) {
  rtmp_chunk_header_t h;
  size_t n;
  h.fmt = RTMP_CHUNK_FMT_0;
  h.cid = cid;
  h.timestamp = 0;
  h.length = (uint32_t)len;
  h.type = type;
  h.stream_id = stream_id;
  n = rtmp_chunk_basic_header_write(out, h.fmt, h.cid);
  n += rtmp_chunk_message_header_write(out + n, &h);
  memcpy(out + n, payload, len);
  return n + len;
}

static inline size_t build_result_create_stream(unsigned char *out, double stream_id) {
  ebuf_t b;
  size_t n;
  memset(&b, 0, sizeof b);
  amf_string(&b, "_result");
  amf_number(&b, 2);
  amf_null(&b);
  amf_number(&b, stream_id);
  n = build_chunk(out, 3, RTMP_TYPE_INVOKE, 0, b.p, b.len);
  ebuf_free(&b);
  return n;
}

static inline channel_t *lookup_ip(channel_table_t *t, const char *ip, unsigned port) {
  unsigned char addr[4];
  inet_pton(AF_INET, ip, addr);
  return channel_lookup(t, AF_INET, addr, sizeof addr, port);
}

static inline int find_tlv(const unsigned char *payload, size_t payload_len, unsigned short want_tag, const unsigned char **val_out, unsigned short *len_out) {
  simulcrypt_tlv_reader_t r;
  unsigned short tag;
  unsigned short vlen;
  const unsigned char *val;
  simulcrypt_tlv_reader_init(&r, payload, payload_len);
  while (simulcrypt_tlv_reader_next(&r, &tag, &val, &vlen) == 1) {
    if (tag == want_tag) {
      *val_out = val;
      *len_out = vlen;
      return 1;
    }
  }
  return 0;
}

#endif
