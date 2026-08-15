/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "chunk.h"

size_t rtmp_chunk_basic_header_write(unsigned char *o, unsigned fmt, uint32_t cid) {
  if (cid >= 64 + 256) {
    o[0] = (unsigned char)((fmt << 6) | 1);
    o[1] = (unsigned char)((cid - 64) & 0xFF);
    o[2] = (unsigned char)(((cid - 64) >> 8) & 0xFF);
    return 3;
  }
  if (cid >= 64) {
    o[0] = (unsigned char)((fmt << 6) | 0);
    o[1] = (unsigned char)(cid - 64);
    return 2;
  }
  o[0] = (unsigned char)((fmt << 6) | (unsigned char)cid);
  return 1;
}

size_t rtmp_chunk_basic_header_read(const unsigned char *d, unsigned *fmt, uint32_t *cid) {
  *fmt = d[0] >> 6;
  *cid = d[0] & 0x3F;
  if (0 == *cid) {
    *cid = 64 + (uint32_t)d[1];
    return 2;
  }
  if (1 == *cid) {
    *cid = 64 + (uint32_t)d[1] + ((uint32_t)d[2] << 8);
    return 3;
  }
  return 1;
}

static void put_u24(unsigned char *o, uint32_t v) {
  o[0] = (unsigned char)(v >> 16);
  o[1] = (unsigned char)(v >> 8);
  o[2] = (unsigned char)v;
}

static uint32_t get_u24(const unsigned char *d) {
  return ((uint32_t)d[0] << 16) | ((uint32_t)d[1] << 8) | d[2];
}

size_t rtmp_chunk_message_header_write(unsigned char *o, const rtmp_chunk_header_t *h) {
  size_t n = 0;

  if (h->fmt <= RTMP_CHUNK_FMT_2) {
    put_u24(o + n, h->timestamp >= 0xFFFFFF ? 0xFFFFFF : h->timestamp);
    n += 3;
  }
  if (h->fmt <= RTMP_CHUNK_FMT_1) {
    put_u24(o + n, h->length);
    o[n + 3] = h->type;
    n += 4;
  }
  if (h->fmt == RTMP_CHUNK_FMT_0) {
    /* message stream id: RTMP's one little-endian field, everything else is big-endian */
    o[n] = (unsigned char)h->stream_id;
    o[n + 1] = (unsigned char)(h->stream_id >> 8);
    o[n + 2] = (unsigned char)(h->stream_id >> 16);
    o[n + 3] = (unsigned char)(h->stream_id >> 24);
    n += 4;
  }
  return n;
}

size_t rtmp_chunk_message_header_read(const unsigned char *d, rtmp_chunk_header_t *h) {
  size_t n = 0;

  if (h->fmt <= RTMP_CHUNK_FMT_2) {
    h->timestamp = get_u24(d + n);
    n += 3;
  }
  if (h->fmt <= RTMP_CHUNK_FMT_1) {
    h->length = get_u24(d + n);
    h->type = d[n + 3];
    n += 4;
  }
  if (h->fmt == RTMP_CHUNK_FMT_0) {
    h->stream_id = (uint32_t)d[n] | ((uint32_t)d[n + 1] << 8) | ((uint32_t)d[n + 2] << 16) | ((uint32_t)d[n + 3] << 24);
    n += 4;
  }
  return n;
}

void rtmp_chunk_extended_timestamp_write(unsigned char *o, uint32_t timestamp) {
  o[0] = (unsigned char)(timestamp >> 24);
  o[1] = (unsigned char)(timestamp >> 16);
  o[2] = (unsigned char)(timestamp >> 8);
  o[3] = (unsigned char)timestamp;
}

uint32_t rtmp_chunk_extended_timestamp_read(const unsigned char *d) {
  return ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3];
}
