/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_CAS_SIMULCRYPT_MSG_H
#define DVBIPITOOLS_LIB_CAS_SIMULCRYPT_MSG_H

#include <stddef.h>

/* TS 103 197 generic_message: version(1) + type(2) + payload_len(2), BE, then TLV payload.
   message_type/parameter_type are interface-specific, defined by callers, not here. */

#define SIMULCRYPT_HDR_LEN 5
#define SIMULCRYPT_MAX_PAYLOAD 65535
#define SIMULCRYPT_MAX_FRAME (SIMULCRYPT_HDR_LEN + SIMULCRYPT_MAX_PAYLOAD)

typedef struct {
  unsigned char version;
  unsigned short type;
  unsigned short payload_len;
} simulcrypt_hdr_t;

/* -1 if buf_len < SIMULCRYPT_HDR_LEN */
int simulcrypt_hdr_parse(const unsigned char *buf, size_t buf_len, simulcrypt_hdr_t *hdr);
/* 0 on overflow, else SIMULCRYPT_HDR_LEN */
size_t simulcrypt_hdr_write(unsigned char version, unsigned short type, unsigned short payload_len, unsigned char *out, size_t cap);

/* zero-copy: value points into caller's buffer */
typedef struct {
  const unsigned char *buf;
  size_t len;
  size_t pos;
} simulcrypt_tlv_reader_t;

void simulcrypt_tlv_reader_init(simulcrypt_tlv_reader_t *r, const unsigned char *payload, size_t payload_len);
/* 1 = element, 0 = clean end, -1 = truncated tag/length or value */
int simulcrypt_tlv_reader_next(simulcrypt_tlv_reader_t *r, unsigned short *tag, const unsigned char **value, unsigned short *value_len);

typedef struct {
  unsigned char *buf;
  size_t cap;
  size_t len; /* 0 after a failed put/begin */
} simulcrypt_writer_t;

/* -1 on overflow */
int simulcrypt_writer_begin(simulcrypt_writer_t *w, unsigned char *buf, size_t cap, unsigned char version, unsigned short type);
/* -1 on overflow */
int simulcrypt_writer_put_tlv(simulcrypt_writer_t *w, unsigned short tag, const unsigned char *value, unsigned short value_len);
/* patches payload_len, returns total frame bytes; 0 if begin/put_tlv ever failed */
size_t simulcrypt_writer_finish(simulcrypt_writer_t *w);

/* buffered non-blocking-socket + poll() single-frame reader, state survives partial reads */
typedef struct {
  unsigned char buf[SIMULCRYPT_MAX_FRAME];
  size_t have;
  size_t need; /* 0 = still reading header */
  simulcrypt_hdr_t hdr;
} simulcrypt_reader_t;

void simulcrypt_reader_init(simulcrypt_reader_t *r);
/* 1 = frame ready (hdr/payload valid till next call, reader reset), 0 = timeout/partial, -1 = EOF/error */
int simulcrypt_reader_poll(simulcrypt_reader_t *r, int fd, int timeout_ms, simulcrypt_hdr_t *hdr, const unsigned char **payload);

/* non-blocking send loop, poll()-with-timeout when the socket buffer is full. 0 ok, -1 on error/timeout */
int simulcrypt_send_all(int fd, const unsigned char *buf, size_t len, int timeout_ms);

#endif
