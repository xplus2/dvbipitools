/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/cas/emmg_server/emmg_server.h"
#include "lib/cas/simulcrypt_msg.h"

static int find_tlv(const unsigned char *payload, size_t payload_len, unsigned short want_tag, const unsigned char **val_out, unsigned short *len_out) {
  simulcrypt_tlv_reader_t r;
  unsigned short tag, vlen;
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

START_TEST(channel_status_builds_expected_fields) {
  unsigned char buf[64];
  simulcrypt_hdr_t hdr;
  const unsigned char *val;
  unsigned short vlen;
  size_t n = emmg_build_channel_status(buf, sizeof buf, 3, 0x4A750001, 7);

  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);
  ck_assert_uint_eq(hdr.version, 3);
  ck_assert_uint_eq(hdr.type, EMMG_MSG_CHANNEL_STATUS);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_CLIENT_ID, &val, &vlen), 1);
  ck_assert_uint_eq(vlen, 4);
  ck_assert_uint_eq(((unsigned)val[0] << 24) | ((unsigned)val[1] << 16) | ((unsigned)val[2] << 8) | val[3], 0x4A750001u);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_DATA_CHANNEL_ID, &val, &vlen), 1);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 7u);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_SECTION_TSPKT_FLAG, &val, &vlen), 1);
  ck_assert_uint_eq(vlen, 1);
  ck_assert_uint_eq(val[0], 0);
}
END_TEST

START_TEST(channel_status_rejects_small_cap) {
  unsigned char buf[5];
  ck_assert_uint_eq(emmg_build_channel_status(buf, sizeof buf, 3, 1, 1), 0u);
}
END_TEST

START_TEST(stream_status_builds_expected_fields) {
  unsigned char buf[64];
  simulcrypt_hdr_t hdr;
  const unsigned char *val;
  unsigned short vlen;
  size_t n = emmg_build_stream_status(buf, sizeof buf, 3, 0x4A750001, 7, 9, 100, 0);

  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);
  ck_assert_uint_eq(hdr.type, EMMG_MSG_STREAM_STATUS);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_DATA_STREAM_ID, &val, &vlen), 1);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 9u);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_DATA_ID, &val, &vlen), 1);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 100u);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_DATA_TYPE, &val, &vlen), 1);
  ck_assert_uint_eq(vlen, 1);
  ck_assert_uint_eq(val[0], 0);
}
END_TEST

START_TEST(stream_status_rejects_small_cap) {
  unsigned char buf[5];
  ck_assert_uint_eq(emmg_build_stream_status(buf, sizeof buf, 3, 1, 1, 1, 1, 0), 0u);
}
END_TEST

START_TEST(stream_close_response_builds_expected_fields) {
  unsigned char buf[64];
  simulcrypt_hdr_t hdr;
  const unsigned char *val;
  unsigned short vlen;
  size_t n = emmg_build_stream_close_response(buf, sizeof buf, 2, 0x11223344, 5, 6);

  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);
  ck_assert_uint_eq(hdr.version, 2);
  ck_assert_uint_eq(hdr.type, EMMG_MSG_STREAM_CLOSE_RESPONSE);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_DATA_STREAM_ID, &val, &vlen), 1);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 6u);
}
END_TEST

START_TEST(stream_close_response_rejects_small_cap) {
  unsigned char buf[5];
  ck_assert_uint_eq(emmg_build_stream_close_response(buf, sizeof buf, 3, 1, 1, 1), 0u);
}
END_TEST

START_TEST(stream_bw_allocation_includes_bandwidth_when_present) {
  unsigned char buf[64];
  simulcrypt_hdr_t hdr;
  const unsigned char *val;
  unsigned short vlen;
  size_t n = emmg_build_stream_bw_allocation(buf, sizeof buf, 3, 1, 1, 1, 1, 512);

  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);
  ck_assert_uint_eq(hdr.type, EMMG_MSG_STREAM_BW_ALLOCATION);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_BANDWIDTH, &val, &vlen), 1);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 512u);
}
END_TEST

START_TEST(stream_bw_allocation_omits_bandwidth_when_absent) {
  unsigned char buf[64];
  simulcrypt_hdr_t hdr;
  const unsigned char *val;
  unsigned short vlen;
  size_t n = emmg_build_stream_bw_allocation(buf, sizeof buf, 3, 1, 1, 1, 0, 0);

  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);
  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, EMMG_P_BANDWIDTH, &val, &vlen), 0);
}
END_TEST

typedef struct {
  unsigned char data[8][32];
  unsigned short len[8];
  int count;
} collected_t;

static void collect_cb(const unsigned char *data, unsigned short len, void *user) {
  collected_t *c = user;
  ck_assert_int_lt(c->count, 8);
  memcpy(c->data[c->count], data, len);
  c->len[c->count] = len;
  c->count++;
}

START_TEST(extract_datagrams_single) {
  unsigned char buf[64];
  simulcrypt_writer_t w;
  collected_t c;
  static const unsigned char dg[] = {0xAA, 0xBB, 0xCC};

  memset(&c, 0, sizeof c);
  simulcrypt_writer_begin(&w, buf, sizeof buf, 3, EMMG_MSG_DATA_PROVISION);
  simulcrypt_writer_put_tlv(&w, EMMG_P_CLIENT_ID, (unsigned char[]){0, 0, 0, 1}, 4);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_ID, (unsigned char[]){0, 1}, 2);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATAGRAM, dg, sizeof dg);
  size_t n = simulcrypt_writer_finish(&w);

  int rc = emmg_extract_datagrams(buf + SIMULCRYPT_HDR_LEN, n - SIMULCRYPT_HDR_LEN, collect_cb, &c);
  ck_assert_int_eq(rc, 1);
  ck_assert_int_eq(c.count, 1);
  ck_assert_uint_eq(c.len[0], sizeof dg);
  ck_assert_mem_eq(c.data[0], dg, sizeof dg);
}
END_TEST

START_TEST(extract_datagrams_multiple_in_order) {
  unsigned char buf[128];
  simulcrypt_writer_t w;
  collected_t c;
  static const unsigned char dg1[] = {1, 2};
  static const unsigned char dg2[] = {3, 4, 5};
  static const unsigned char dg3[] = {6};

  memset(&c, 0, sizeof c);
  simulcrypt_writer_begin(&w, buf, sizeof buf, 3, EMMG_MSG_DATA_PROVISION);
  simulcrypt_writer_put_tlv(&w, EMMG_P_CLIENT_ID, (unsigned char[]){0, 0, 0, 1}, 4);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_CHANNEL_ID, (unsigned char[]){0, 1}, 2);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_STREAM_ID, (unsigned char[]){0, 1}, 2);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_ID, (unsigned char[]){0, 1}, 2);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATAGRAM, dg1, sizeof dg1);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATAGRAM, dg2, sizeof dg2);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATAGRAM, dg3, sizeof dg3);
  size_t n = simulcrypt_writer_finish(&w);

  int rc = emmg_extract_datagrams(buf + SIMULCRYPT_HDR_LEN, n - SIMULCRYPT_HDR_LEN, collect_cb, &c);
  ck_assert_int_eq(rc, 3);
  ck_assert_int_eq(c.count, 3);
  ck_assert_mem_eq(c.data[0], dg1, sizeof dg1);
  ck_assert_mem_eq(c.data[1], dg2, sizeof dg2);
  ck_assert_mem_eq(c.data[2], dg3, sizeof dg3);
}
END_TEST

START_TEST(extract_datagrams_rejects_malformed) {
  static const unsigned char buf[] = {0x00, 0x05, 0x00}; /* truncated tag/length */
  collected_t c;
  memset(&c, 0, sizeof c);
  int rc = emmg_extract_datagrams(buf, sizeof buf, collect_cb, &c);
  ck_assert_int_eq(rc, -1);
}
END_TEST

/* below: standalone integration tests. a synchronous fake EMMG client
   (this process, a loopback socket) connects to the REAL emmg_server.c listener and drives
   its actual accept/worker state machine end to end. */

static int fake_connect(unsigned port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((unsigned short)port);
  if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static size_t fake_build_channel_setup(unsigned char *out, size_t cap, unsigned char version, unsigned client_id, unsigned data_channel_id) {
  simulcrypt_writer_t w;
  unsigned char cid[4];
  cid[0] = (unsigned char)(client_id >> 24);
  cid[1] = (unsigned char)(client_id >> 16);
  cid[2] = (unsigned char)(client_id >> 8);
  cid[3] = (unsigned char)client_id;
  simulcrypt_writer_begin(&w, out, cap, version, EMMG_MSG_CHANNEL_SETUP);
  simulcrypt_writer_put_tlv(&w, EMMG_P_CLIENT_ID, cid, 4);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_CHANNEL_ID, (unsigned char[]){(unsigned char)(data_channel_id >> 8), (unsigned char)data_channel_id}, 2);
  return simulcrypt_writer_finish(&w);
}

static size_t fake_build_stream_setup(unsigned char *out, size_t cap, unsigned char version, unsigned data_stream_id, unsigned data_id, unsigned data_type) {
  simulcrypt_writer_t w;
  simulcrypt_writer_begin(&w, out, cap, version, EMMG_MSG_STREAM_SETUP);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_STREAM_ID, (unsigned char[]){(unsigned char)(data_stream_id >> 8), (unsigned char)data_stream_id}, 2);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_ID, (unsigned char[]){(unsigned char)(data_id >> 8), (unsigned char)data_id}, 2);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATA_TYPE, (unsigned char[]){(unsigned char)data_type}, 1);
  return simulcrypt_writer_finish(&w);
}

static size_t fake_build_stream_bw_request(unsigned char *out, size_t cap, unsigned char version, unsigned bandwidth_kbps) {
  simulcrypt_writer_t w;
  simulcrypt_writer_begin(&w, out, cap, version, EMMG_MSG_STREAM_BW_REQUEST);
  simulcrypt_writer_put_tlv(&w, EMMG_P_BANDWIDTH, (unsigned char[]){(unsigned char)(bandwidth_kbps >> 8), (unsigned char)bandwidth_kbps}, 2);
  return simulcrypt_writer_finish(&w);
}

static size_t fake_build_data_provision(unsigned char *out, size_t cap, unsigned char version, const unsigned char *dg, size_t dg_len) {
  simulcrypt_writer_t w;
  simulcrypt_writer_begin(&w, out, cap, version, EMMG_MSG_DATA_PROVISION);
  simulcrypt_writer_put_tlv(&w, EMMG_P_DATAGRAM, dg, (unsigned short)dg_len);
  return simulcrypt_writer_finish(&w);
}

static int fake_read_reply(int fd, simulcrypt_hdr_t *hdr, unsigned char *payload_out, size_t cap) {
  simulcrypt_reader_t rd;
  const unsigned char *payload;
  int rc;
  simulcrypt_reader_init(&rd);
  rc = simulcrypt_reader_poll(&rd, fd, 3000, hdr, &payload);
  if (rc != 1)
    return -1;
  if (hdr->payload_len > cap)
    return -1;
  memcpy(payload_out, payload, hdr->payload_len);
  return 0;
}

START_TEST(emmg_server_completes_real_handshake_and_queues_datagram) {
  emmg_server_cfg_t cfg;
  emmg_server_t *s;
  unsigned port;
  int fd;
  unsigned char msg[512], payload[512];
  simulcrypt_hdr_t hdr;
  size_t n;
  static const unsigned char dg[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01};
  unsigned char got[64];
  size_t got_len;
  int waited;

  cfg.port = 0; /* kernel-assigned ephemeral port */
  s = emmg_server_start(&cfg);
  ck_assert_ptr_nonnull(s);
  port = emmg_server_port(s);
  ck_assert_uint_ne(port, 0u);

  fd = fake_connect(port);
  ck_assert_int_ge(fd, 0);

  n = fake_build_channel_setup(msg, sizeof msg, 3, 0x4A750001, 1);
  ck_assert_int_eq(simulcrypt_send_all(fd, msg, n, 3000), 0);
  ck_assert_int_eq(fake_read_reply(fd, &hdr, payload, sizeof payload), 0);
  ck_assert_uint_eq(hdr.type, EMMG_MSG_CHANNEL_STATUS);

  n = fake_build_stream_setup(msg, sizeof msg, 3, 1, 1, 0);
  ck_assert_int_eq(simulcrypt_send_all(fd, msg, n, 3000), 0);
  ck_assert_int_eq(fake_read_reply(fd, &hdr, payload, sizeof payload), 0);
  ck_assert_uint_eq(hdr.type, EMMG_MSG_STREAM_STATUS);

  n = fake_build_stream_bw_request(msg, sizeof msg, 3, 512);
  ck_assert_int_eq(simulcrypt_send_all(fd, msg, n, 3000), 0);
  ck_assert_int_eq(fake_read_reply(fd, &hdr, payload, sizeof payload), 0);
  ck_assert_uint_eq(hdr.type, EMMG_MSG_STREAM_BW_ALLOCATION);

  n = fake_build_data_provision(msg, sizeof msg, 3, dg, sizeof dg);
  ck_assert_int_eq(simulcrypt_send_all(fd, msg, n, 3000), 0);

  waited = 0;
  while (emmg_server_dequeue_emm(s, got, sizeof got, &got_len) != 0 && waited < 3000) {
    struct timespec ts = {0, 20L * 1000000L};
    nanosleep(&ts, NULL);
    waited += 20;
  }
  ck_assert_uint_eq(got_len, sizeof dg);
  ck_assert_mem_eq(got, dg, sizeof dg);

  close(fd);
  emmg_server_stop(s);
}
END_TEST

START_TEST(emmg_server_accepts_up_to_new_conn_cap) {
  emmg_server_cfg_t cfg;
  emmg_server_t *s;
  unsigned port;
  int fd[8], extra;
  int i;
  unsigned char msg[512], payload[512];
  simulcrypt_hdr_t hdr;
  size_t n;

  cfg.port = 0;
  s = emmg_server_start(&cfg);
  ck_assert_ptr_nonnull(s);
  port = emmg_server_port(s);

  /* old cap was 4 - all 8 of these must complete channel_setup */
  for (i = 0; i < 8; i++) {
    fd[i] = fake_connect(port);
    ck_assert_int_ge(fd[i], 0);
    n = fake_build_channel_setup(msg, sizeof msg, 3, 0x4A750000u + (unsigned)i, 1);
    ck_assert_int_eq(simulcrypt_send_all(fd[i], msg, n, 3000), 0);
    ck_assert_int_eq(fake_read_reply(fd[i], &hdr, payload, sizeof payload), 0);
    ck_assert_uint_eq(hdr.type, EMMG_MSG_CHANNEL_STATUS);
  }

  /* 9th, beyond the new cap: TCP accepts it, the server then closes it */
  extra = fake_connect(port);
  ck_assert_int_ge(extra, 0);
  n = fake_build_channel_setup(msg, sizeof msg, 3, 0x4A7500FFu, 1);
  simulcrypt_send_all(extra, msg, n, 3000);
  ck_assert_int_eq(fake_read_reply(extra, &hdr, payload, sizeof payload), -1);
  close(extra);

  for (i = 0; i < 8; i++)
    close(fd[i]);
  emmg_server_stop(s);
}
END_TEST

START_TEST(emmg_server_queue_holds_more_than_old_64_cap) {
  emmg_server_cfg_t cfg;
  emmg_server_t *s;
  unsigned port;
  int fd;
  unsigned char msg[512], payload[512];
  simulcrypt_hdr_t hdr;
  size_t n;
  int i;
  unsigned char got[64];
  size_t got_len;
  struct timespec ts;

  cfg.port = 0;
  s = emmg_server_start(&cfg);
  ck_assert_ptr_nonnull(s);
  port = emmg_server_port(s);
  fd = fake_connect(port);
  ck_assert_int_ge(fd, 0);

  n = fake_build_channel_setup(msg, sizeof msg, 3, 0x4A750002, 1);
  simulcrypt_send_all(fd, msg, n, 3000);
  fake_read_reply(fd, &hdr, payload, sizeof payload);
  n = fake_build_stream_setup(msg, sizeof msg, 3, 1, 1, 0);
  simulcrypt_send_all(fd, msg, n, 3000);
  fake_read_reply(fd, &hdr, payload, sizeof payload);

  /* old cap was 64 - push 65 without draining, first one must survive */
  for (i = 0; i < 65; i++) {
    unsigned char dg[1];
    dg[0] = (unsigned char)i;
    n = fake_build_data_provision(msg, sizeof msg, 3, dg, sizeof dg);
    ck_assert_int_eq(simulcrypt_send_all(fd, msg, n, 3000), 0);
  }

  ts.tv_sec = 0;
  ts.tv_nsec = 300L * 1000000L;
  nanosleep(&ts, NULL);

  ck_assert_int_eq(emmg_server_dequeue_emm(s, got, sizeof got, &got_len), 0);
  ck_assert_uint_eq(got_len, 1u);
  ck_assert_uint_eq(got[0], 0u);

  close(fd);
  emmg_server_stop(s);
}
END_TEST

START_TEST(emmg_server_rejects_stream_setup_before_channel_setup) {
  emmg_server_cfg_t cfg;
  emmg_server_t *s;
  unsigned port;
  int fd;
  unsigned char msg[512];
  size_t n;
  ssize_t rn;

  cfg.port = 0;
  s = emmg_server_start(&cfg);
  ck_assert_ptr_nonnull(s);
  port = emmg_server_port(s);

  fd = fake_connect(port);
  ck_assert_int_ge(fd, 0);

  n = fake_build_stream_setup(msg, sizeof msg, 3, 1, 1, 0);
  ck_assert_int_eq(simulcrypt_send_all(fd, msg, n, 3000), 0);

  /* server closes the connection (should_close) instead of replying: read() sees EOF */
  rn = read(fd, msg, sizeof msg);
  ck_assert_int_eq(rn, 0);

  close(fd);
  emmg_server_stop(s);
}
END_TEST

static Suite *emmg_server_suite(void) {
  Suite *s = suite_create("emmg_server");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, channel_status_builds_expected_fields);
  tcase_add_test(tc, channel_status_rejects_small_cap);
  tcase_add_test(tc, stream_status_builds_expected_fields);
  tcase_add_test(tc, stream_status_rejects_small_cap);
  tcase_add_test(tc, stream_close_response_builds_expected_fields);
  tcase_add_test(tc, stream_close_response_rejects_small_cap);
  tcase_add_test(tc, stream_bw_allocation_includes_bandwidth_when_present);
  tcase_add_test(tc, stream_bw_allocation_omits_bandwidth_when_absent);
  tcase_add_test(tc, extract_datagrams_single);
  tcase_add_test(tc, extract_datagrams_multiple_in_order);
  tcase_add_test(tc, extract_datagrams_rejects_malformed);
  suite_add_tcase(s, tc);

  {
    /* real sockets: give this tcase more headroom than the default */
    TCase *tc_integ = tcase_create("integration");
    tcase_set_timeout(tc_integ, 15);
    tcase_add_test(tc_integ, emmg_server_completes_real_handshake_and_queues_datagram);
    tcase_add_test(tc_integ, emmg_server_rejects_stream_setup_before_channel_setup);
    tcase_add_test(tc_integ, emmg_server_accepts_up_to_new_conn_cap);
    tcase_add_test(tc_integ, emmg_server_queue_holds_more_than_old_64_cap);
    suite_add_tcase(s, tc_integ);
  }

  return s;
}

int main(void) {
  SRunner *sr = srunner_create(emmg_server_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
