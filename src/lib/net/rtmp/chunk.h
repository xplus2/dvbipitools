/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_RTMP_CHUNK_H
#define DVBIPITOOLS_LIB_NET_RTMP_CHUNK_H

#include <stddef.h>
#include <stdint.h>

#define RTMP_CHUNK_FMT_0 0
#define RTMP_CHUNK_FMT_1 1
#define RTMP_CHUNK_FMT_2 2
#define RTMP_CHUNK_FMT_3 3

typedef struct {
  unsigned fmt;
  uint32_t cid;
  uint32_t timestamp;
  uint32_t length;
  unsigned char type;
  uint32_t stream_id; /* little-endian on wire, unlike every other RTMP field */
} rtmp_chunk_header_t;

size_t rtmp_chunk_basic_header_write(unsigned char *o, unsigned fmt, uint32_t cid);
size_t rtmp_chunk_basic_header_read(const unsigned char *d, unsigned *fmt, uint32_t *cid);

size_t rtmp_chunk_message_header_write(unsigned char *o, const rtmp_chunk_header_t *h);
size_t rtmp_chunk_message_header_read(const unsigned char *d, rtmp_chunk_header_t *h);

void rtmp_chunk_extended_timestamp_write(unsigned char *o, uint32_t timestamp);
uint32_t rtmp_chunk_extended_timestamp_read(const unsigned char *d);

#endif
