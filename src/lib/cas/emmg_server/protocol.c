/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/mux/psi_build.h"

#include "../simulcrypt_msg.h"
#include "priv.h"

size_t emmg_build_channel_status(unsigned char *out, size_t cap, unsigned char version, unsigned client_id, unsigned data_channel_id) {
  simulcrypt_writer_t w;
  unsigned char cid[4];
  psi_put16(cid, client_id >> 16);
  psi_put16(cid + 2, client_id);
  if (simulcrypt_writer_begin(&w, out, cap, version, EMMG_MSG_CHANNEL_STATUS) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, EMMG_P_CLIENT_ID, cid, sizeof cid) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_CHANNEL_ID, (unsigned char[]){(unsigned char)(data_channel_id >> 8), (unsigned char)data_channel_id}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, EMMG_P_SECTION_TSPKT_FLAG, (unsigned char[]){0x00}, 1) < 0)
    return 0;
  return simulcrypt_writer_finish(&w);
}

size_t emmg_build_stream_status(unsigned char *out, size_t cap, unsigned char version, unsigned client_id, unsigned data_channel_id, unsigned data_stream_id, unsigned data_id, unsigned data_type) {
  simulcrypt_writer_t w;
  unsigned char cid[4];
  psi_put16(cid, client_id >> 16);
  psi_put16(cid + 2, client_id);
  if (simulcrypt_writer_begin(&w, out, cap, version, EMMG_MSG_STREAM_STATUS) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, EMMG_P_CLIENT_ID, cid, sizeof cid) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_CHANNEL_ID, (unsigned char[]){(unsigned char)(data_channel_id >> 8), (unsigned char)data_channel_id}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_STREAM_ID, (unsigned char[]){(unsigned char)(data_stream_id >> 8), (unsigned char)data_stream_id}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_ID, (unsigned char[]){(unsigned char)(data_id >> 8), (unsigned char)data_id}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_TYPE, (unsigned char[]){(unsigned char)data_type}, 1) < 0)
    return 0;
  return simulcrypt_writer_finish(&w);
}

size_t emmg_build_stream_close_response(unsigned char *out, size_t cap, unsigned char version, unsigned client_id, unsigned data_channel_id, unsigned data_stream_id) {
  simulcrypt_writer_t w;
  unsigned char cid[4];
  psi_put16(cid, client_id >> 16);
  psi_put16(cid + 2, client_id);
  if (simulcrypt_writer_begin(&w, out, cap, version, EMMG_MSG_STREAM_CLOSE_RESPONSE) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, EMMG_P_CLIENT_ID, cid, sizeof cid) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_CHANNEL_ID, (unsigned char[]){(unsigned char)(data_channel_id >> 8), (unsigned char)data_channel_id}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_STREAM_ID, (unsigned char[]){(unsigned char)(data_stream_id >> 8), (unsigned char)data_stream_id}, 2) < 0)
    return 0;
  return simulcrypt_writer_finish(&w);
}

size_t emmg_build_stream_bw_allocation(unsigned char *out, size_t cap, unsigned char version, unsigned client_id, unsigned data_channel_id, unsigned data_stream_id, int have_bandwidth, unsigned bandwidth_kbps) {
  simulcrypt_writer_t w;
  unsigned char cid[4];
  psi_put16(cid, client_id >> 16);
  psi_put16(cid + 2, client_id);
  if (simulcrypt_writer_begin(&w, out, cap, version, EMMG_MSG_STREAM_BW_ALLOCATION) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, EMMG_P_CLIENT_ID, cid, sizeof cid) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_CHANNEL_ID, (unsigned char[]){(unsigned char)(data_channel_id >> 8), (unsigned char)data_channel_id}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_STREAM_ID, (unsigned char[]){(unsigned char)(data_stream_id >> 8), (unsigned char)data_stream_id}, 2) < 0)
    return 0;
  if (have_bandwidth && simulcrypt_writer_put_tlv(&w, EMMG_P_BANDWIDTH, (unsigned char[]){(unsigned char)(bandwidth_kbps >> 8), (unsigned char)bandwidth_kbps}, 2) < 0)
    return 0;
  return simulcrypt_writer_finish(&w);
}

int emmg_extract_datagrams(const unsigned char *body, size_t body_len, emmg_datagram_cb cb, void *user) {
  simulcrypt_tlv_reader_t it;
  unsigned short tag, vlen;
  const unsigned char *val;
  int rc, count = 0;
  simulcrypt_tlv_reader_init(&it, body, body_len);
  while ((rc = simulcrypt_tlv_reader_next(&it, &tag, &val, &vlen)) == 1) {
    if (tag == EMMG_P_DATAGRAM) {
      cb(val, vlen, user);
      count++;
    }
  }
  return rc < 0 ? -1 : count;
}
