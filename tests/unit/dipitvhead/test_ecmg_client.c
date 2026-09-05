/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/cas/ecmg_client/ecmg_client.h"
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

START_TEST(channel_setup_builds_expected_fields) {
  unsigned char buf[64];
  simulcrypt_hdr_t hdr;
  const unsigned char *val;
  unsigned short vlen;
  size_t n = ecmg_build_channel_setup(buf, sizeof buf, 3, 0x4A750002);

  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);
  ck_assert_uint_eq(hdr.version, 3);
  ck_assert_uint_eq(hdr.type, ECMG_MSG_CHANNEL_SETUP);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, ECMG_P_ECM_CHANNEL_ID, &val, &vlen), 1);
  ck_assert_uint_eq(vlen, 2);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], ECMG_CHANNEL_ID);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, ECMG_P_SUPER_CAS_ID, &val, &vlen), 1);
  ck_assert_uint_eq(vlen, 4);
  ck_assert_uint_eq(((unsigned)val[0] << 24) | ((unsigned)val[1] << 16) | ((unsigned)val[2] << 8) | val[3], 0x4A750002u);
}
END_TEST

START_TEST(channel_setup_rejects_small_cap) {
  unsigned char buf[5];
  ck_assert_uint_eq(ecmg_build_channel_setup(buf, sizeof buf, 3, 1), 0u);
}
END_TEST

START_TEST(stream_setup_builds_expected_fields) {
  unsigned char buf[64];
  simulcrypt_hdr_t hdr;
  const unsigned char *val;
  unsigned short vlen;
  size_t n = ecmg_build_stream_setup(buf, sizeof buf, 3, 0x1234, 100);

  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);
  ck_assert_uint_eq(hdr.type, ECMG_MSG_STREAM_SETUP);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, ECMG_P_ECM_STREAM_ID, &val, &vlen), 1);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], ECMG_STREAM_ID);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, ECMG_P_ECM_ID, &val, &vlen), 1);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 0x1234u);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, ECMG_P_NOMINAL_CP_DURATION, &val, &vlen), 1);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 100u);
}
END_TEST

START_TEST(stream_setup_rejects_small_cap) {
  unsigned char buf[5];
  ck_assert_uint_eq(ecmg_build_stream_setup(buf, sizeof buf, 3, 1, 1), 0u);
}
END_TEST

START_TEST(cw_provision_lead0_permsg1_sends_one_combo_at_cp) {
  unsigned char buf[128];
  cw_hist_entry_t hist[ECMG_CW_HIST];
  simulcrypt_hdr_t hdr;
  simulcrypt_tlv_reader_t r;
  unsigned short tag, vlen;
  const unsigned char *val;
  int combos = 0;
  cwenc_ctx_t off_ctx = {0};

  memset(hist, 0, sizeof hist);
  size_t n = ecmg_build_cw_provision(buf, sizeof buf, 3, 500, hist, 8, 0, 1, &off_ctx);
  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);
  ck_assert_uint_eq(hdr.type, ECMG_MSG_CW_PROVISION);

  simulcrypt_tlv_reader_init(&r, buf + SIMULCRYPT_HDR_LEN, hdr.payload_len);
  while (simulcrypt_tlv_reader_next(&r, &tag, &val, &vlen) == 1) {
    if (tag == ECMG_P_CP_CW_COMBINATION) {
      ck_assert_uint_eq(vlen, 2 + 8);
      ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 500);
      combos++;
    }
  }
  ck_assert_int_eq(combos, 1);
}
END_TEST

START_TEST(cw_provision_lead1_permsg2_sends_current_and_next) {
  unsigned char buf[128];
  cw_hist_entry_t hist[ECMG_CW_HIST];
  simulcrypt_hdr_t hdr;
  simulcrypt_tlv_reader_t r;
  unsigned short tag, vlen;
  const unsigned char *val;
  unsigned short seen_cp[4];
  int combos = 0;
  cwenc_ctx_t off_ctx = {0};
  memset(hist, 0, sizeof hist);
  size_t n = ecmg_build_cw_provision(buf, sizeof buf, 3, 500, hist, 16, 1, 2, &off_ctx);
  ck_assert_uint_gt(n, 0u);
  ck_assert_int_eq(simulcrypt_hdr_parse(buf, n, &hdr), 0);

  simulcrypt_tlv_reader_init(&r, buf + SIMULCRYPT_HDR_LEN, hdr.payload_len);
  while (simulcrypt_tlv_reader_next(&r, &tag, &val, &vlen) == 1) {
    if (tag == ECMG_P_CP_CW_COMBINATION) {
      ck_assert_uint_eq(vlen, 2 + 16);
      ck_assert_int_lt(combos, 4);
      seen_cp[combos] = (unsigned short)(((unsigned)val[0] << 8) | val[1]);
      combos++;
    }
  }
  ck_assert_int_eq(combos, 2);
  ck_assert_uint_eq(seen_cp[0], 500);
  ck_assert_uint_eq(seen_cp[1], 501);
}
END_TEST

START_TEST(cw_provision_reuses_history_for_overlapping_cp) {
  unsigned char buf1[128], buf2[128];
  cw_hist_entry_t hist[ECMG_CW_HIST];
  simulcrypt_hdr_t hdr;
  simulcrypt_tlv_reader_t r;
  unsigned short tag, vlen;
  const unsigned char *val;
  unsigned char cw_cp501_first[16];
  int found;
  cwenc_ctx_t off_ctx = {0};

  memset(hist, 0, sizeof hist);
  /* CP=500: combo for [500,501] - caches CW(501) */
  size_t n1 = ecmg_build_cw_provision(buf1, sizeof buf1, 3, 500, hist, 16, 1, 2, &off_ctx);
  ck_assert_uint_gt(n1, 0u);
  simulcrypt_hdr_parse(buf1, n1, &hdr);
  simulcrypt_tlv_reader_init(&r, buf1 + SIMULCRYPT_HDR_LEN, hdr.payload_len);
  found = 0;
  while (simulcrypt_tlv_reader_next(&r, &tag, &val, &vlen) == 1) {
    if (tag == ECMG_P_CP_CW_COMBINATION && (((unsigned)val[0] << 8) | val[1]) == 501) {
      memcpy(cw_cp501_first, val + 2, 16);
      found = 1;
    }
  }
  ck_assert_int_eq(found, 1);

  /* CP=501: combo for [501,502] - CW(501) must be the SAME value cached above, not fresh randomness */
  size_t n2 = ecmg_build_cw_provision(buf2, sizeof buf2, 3, 501, hist, 16, 1, 2, &off_ctx);
  ck_assert_uint_gt(n2, 0u);
  simulcrypt_hdr_parse(buf2, n2, &hdr);
  simulcrypt_tlv_reader_init(&r, buf2 + SIMULCRYPT_HDR_LEN, hdr.payload_len);
  found = 0;
  while (simulcrypt_tlv_reader_next(&r, &tag, &val, &vlen) == 1) {
    if (tag == ECMG_P_CP_CW_COMBINATION && (((unsigned)val[0] << 8) | val[1]) == 501) {
      ck_assert_mem_eq(val + 2, cw_cp501_first, 16);
      found = 1;
    }
  }
  ck_assert_int_eq(found, 1);
}
END_TEST

START_TEST(cw_provision_rejects_small_cap) {
  unsigned char buf[10];
  cw_hist_entry_t hist[ECMG_CW_HIST];
  cwenc_ctx_t off_ctx = {0};
  memset(hist, 0, sizeof hist);
  ck_assert_uint_eq(ecmg_build_cw_provision(buf, sizeof buf, 3, 1, hist, 16, 0, 1, &off_ctx), 0u);
}
END_TEST

START_TEST(cw_provision_encrypts_when_cwenc_active) {
  unsigned char buf[128];
  cw_hist_entry_t hist[ECMG_CW_HIST];
  simulcrypt_hdr_t hdr;
  const unsigned char *val;
  unsigned short vlen;
  cwenc_config_t cfg;
  cwenc_ctx_t ctx, ctx2;
  cwenc_selection_t sel;
  unsigned char plain_cw[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  unsigned char expect_cw[8];
  size_t n;

  memset(hist, 0, sizeof hist);
  hist[500 % ECMG_CW_HIST].valid = 1;
  hist[500 % ECMG_CW_HIST].cp_number = 500;
  memcpy(hist[500 % ECMG_CW_HIST].cw, plain_cw, 8);

  ck_assert_int_eq(cwenc_config_init(&cfg, "des56", NULL, NULL, NULL, NULL), 0);
  cwenc_ctx_init(&ctx, &cfg);
  cwenc_ctx_init(&ctx2, &cfg);
  ck_assert_int_eq(cwenc_select_next(&ctx2, &sel), 0);
  memcpy(expect_cw, plain_cw, 8);
  ck_assert_int_eq(cwenc_encrypt_cw(&cfg, &sel, 8, ECMG_CHANNEL_ID, ECMG_STREAM_ID, 500, expect_cw), 0);

  n = ecmg_build_cw_provision(buf, sizeof buf, 3, 500, hist, 8, 0, 1, &ctx);
  ck_assert_uint_gt(n, 0u);
  simulcrypt_hdr_parse(buf, n, &hdr);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, ECMG_P_CW_ENCRYPTION, &val, &vlen), 1);
  ck_assert_uint_eq(vlen, 2);
  ck_assert_uint_eq(((unsigned)val[0] << 8) | val[1], 0);

  ck_assert_int_eq(find_tlv(buf + SIMULCRYPT_HDR_LEN, hdr.payload_len, ECMG_P_CP_CW_COMBINATION, &val, &vlen), 1);
  ck_assert_uint_eq(vlen, 2 + 8);
  ck_assert_mem_eq(val + 2, expect_cw, 8);
  ck_assert_mem_ne(val + 2, plain_cw, 8);
}
END_TEST

START_TEST(find_error_status_locates_tag) {
  unsigned char buf[32];
  simulcrypt_writer_t w;
  unsigned short err;
  simulcrypt_writer_begin(&w, buf, sizeof buf, 3, ECMG_MSG_CHANNEL_ERROR);
  simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_CHANNEL_ID, (unsigned char[]){0, 1}, 2);
  simulcrypt_writer_put_tlv(&w, ECMG_P_ERROR_STATUS, (unsigned char[]){0x00, 0x02}, 2);
  size_t n = simulcrypt_writer_finish(&w);

  ck_assert_int_eq(ecmg_find_error_status(buf + SIMULCRYPT_HDR_LEN, n - SIMULCRYPT_HDR_LEN, &err), 0);
  ck_assert_uint_eq(err, ECMG_ERR_UNSUPPORTED_PROTOCOL_VERSION);
}
END_TEST

START_TEST(find_error_status_absent_returns_error) {
  unsigned char buf[32];
  simulcrypt_writer_t w;
  unsigned short err;
  simulcrypt_writer_begin(&w, buf, sizeof buf, 3, ECMG_MSG_CHANNEL_ERROR);
  simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_CHANNEL_ID, (unsigned char[]){0, 1}, 2);
  size_t n = simulcrypt_writer_finish(&w);

  ck_assert_int_eq(ecmg_find_error_status(buf + SIMULCRYPT_HDR_LEN, n - SIMULCRYPT_HDR_LEN, &err), -1);
}
END_TEST

START_TEST(parse_channel_status_full_message) {
  unsigned char buf[64];
  simulcrypt_writer_t w;
  unsigned lead_cw, cw_per_msg, max_comp_time_ms, min_cp_100ms, ecm_rep_period_ms;

  simulcrypt_writer_begin(&w, buf, sizeof buf, 3, ECMG_MSG_CHANNEL_STATUS);
  simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_CHANNEL_ID, (unsigned char[]){0, 1}, 2);
  simulcrypt_writer_put_tlv(&w, ECMG_P_LEAD_CW, (unsigned char[]){1}, 1);
  simulcrypt_writer_put_tlv(&w, ECMG_P_CW_PER_MSG, (unsigned char[]){2}, 1);
  simulcrypt_writer_put_tlv(&w, ECMG_P_MAX_COMP_TIME, (unsigned char[]){0x00, 0x64}, 2);
  simulcrypt_writer_put_tlv(&w, ECMG_P_MIN_CP_DURATION, (unsigned char[]){0x00, 0x0A}, 2);
  simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_REP_PERIOD, (unsigned char[]){0x00, 0x64}, 2);
  size_t n = simulcrypt_writer_finish(&w);

  ck_assert_int_eq(ecmg_parse_channel_status(buf + SIMULCRYPT_HDR_LEN, n - SIMULCRYPT_HDR_LEN, &lead_cw, &cw_per_msg, &max_comp_time_ms, &min_cp_100ms, &ecm_rep_period_ms), 0);
  ck_assert_uint_eq(lead_cw, 1);
  ck_assert_uint_eq(cw_per_msg, 2);
  ck_assert_uint_eq(max_comp_time_ms, 100);
  ck_assert_uint_eq(min_cp_100ms, 10);
  ck_assert_uint_eq(ecm_rep_period_ms, 100);
}
END_TEST

START_TEST(parse_channel_status_missing_cw_per_msg_fails) {
  unsigned char buf[64];
  simulcrypt_writer_t w;
  unsigned lead_cw, cw_per_msg, max_comp_time_ms, min_cp_100ms, ecm_rep_period_ms;

  simulcrypt_writer_begin(&w, buf, sizeof buf, 3, ECMG_MSG_CHANNEL_STATUS);
  simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_CHANNEL_ID, (unsigned char[]){0, 1}, 2);
  simulcrypt_writer_put_tlv(&w, ECMG_P_LEAD_CW, (unsigned char[]){1}, 1);
  size_t n = simulcrypt_writer_finish(&w);

  ck_assert_int_eq(ecmg_parse_channel_status(buf + SIMULCRYPT_HDR_LEN, n - SIMULCRYPT_HDR_LEN, &lead_cw, &cw_per_msg, &max_comp_time_ms, &min_cp_100ms, &ecm_rep_period_ms), -1);
}
END_TEST

START_TEST(parse_channel_status_rejects_out_of_range_cw_per_msg) {
  unsigned char buf[64];
  simulcrypt_writer_t w;
  unsigned lead_cw, cw_per_msg, max_comp_time_ms, min_cp_100ms, ecm_rep_period_ms;

  simulcrypt_writer_begin(&w, buf, sizeof buf, 3, ECMG_MSG_CHANNEL_STATUS);
  simulcrypt_writer_put_tlv(&w, ECMG_P_CW_PER_MSG, (unsigned char[]){(unsigned char)(ECMG_MAX_CW_PER_MSG + 1)}, 1);
  size_t n = simulcrypt_writer_finish(&w);

  ck_assert_int_eq(ecmg_parse_channel_status(buf + SIMULCRYPT_HDR_LEN, n - SIMULCRYPT_HDR_LEN, &lead_cw, &cw_per_msg, &max_comp_time_ms, &min_cp_100ms, &ecm_rep_period_ms), -1);
}
END_TEST

START_TEST(ecm_available_frozen_always_available) {
  ck_assert_int_eq(ecmg_ecm_available_calc(ECMG_OUTAGE_FROZEN, 0), 1);
  ck_assert_int_eq(ecmg_ecm_available_calc(ECMG_OUTAGE_FROZEN, 1), 1);
}
END_TEST

START_TEST(ecm_available_cycling_always_available) {
  ck_assert_int_eq(ecmg_ecm_available_calc(ECMG_OUTAGE_CYCLING, 0), 1);
  ck_assert_int_eq(ecmg_ecm_available_calc(ECMG_OUTAGE_CYCLING, 1), 1);
}
END_TEST

START_TEST(ecm_available_silent_connected_is_available) {
  ck_assert_int_eq(ecmg_ecm_available_calc(ECMG_OUTAGE_SILENT, 1), 1);
}
END_TEST

START_TEST(ecm_available_silent_disconnected_is_unavailable) {
  ck_assert_int_eq(ecmg_ecm_available_calc(ECMG_OUTAGE_SILENT, 0), 0);
}
END_TEST

START_TEST(target_parity_frozen_ignores_elapsed_time) {
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_OUTAGE_FROZEN, 0, 10, 1000, 0, 1), 1);
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_OUTAGE_FROZEN, 0, 10, 1000, 0, 0), 0);
}
END_TEST

START_TEST(target_parity_cycling_connected_uses_last_parity_directly) {
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_OUTAGE_CYCLING, 1, 10, 100000, 0, 1), 1);
}
END_TEST

START_TEST(target_parity_cycling_disconnected_flips_after_one_cp) {
  /* odd, one whole CP elapsed since publish -> flips to even */
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_OUTAGE_CYCLING, 0, 10, 110, 100, 1), 0);
}
END_TEST

START_TEST(target_parity_cycling_disconnected_flips_back_after_two_cp) {
  /* two whole CPs elapsed -> back to the original parity */
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_OUTAGE_CYCLING, 0, 10, 120, 100, 1), 1);
}
END_TEST

START_TEST(target_parity_cycling_disconnected_within_cp_unchanged) {
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_OUTAGE_CYCLING, 0, 10, 105, 100, 1), 1);
}
END_TEST

/* below: standalone integration tests. a small fake ECMG server
   (this process, a loopback socket, a real accept/reply thread) drives the REAL ecmg_client.c
   state machine end to end: connect, handshake, CW_provision/ECM_response, version
   fallback, reconnect after a dropped connection. */

typedef struct {
  int listen_fd;
  unsigned port;
  pthread_t thread;
  atomic_int stop;
  atomic_int reject_first_version; /* channel_error once, then accept the retry */
  atomic_int close_after_first_ecm; /* drop the connection right after one ECM_response */
  atomic_int connections_seen;
} fake_ecmg_t;

static size_t fake_build_channel_status(unsigned char *out, size_t cap, unsigned char version) {
  simulcrypt_writer_t w;
  unsigned char cw_per_msg = 1;
  simulcrypt_writer_begin(&w, out, cap, version, ECMG_MSG_CHANNEL_STATUS);
  simulcrypt_writer_put_tlv(&w, ECMG_P_CW_PER_MSG, &cw_per_msg, 1);
  return simulcrypt_writer_finish(&w);
}

static size_t fake_build_stream_status(unsigned char *out, size_t cap, unsigned char version) {
  simulcrypt_writer_t w;
  simulcrypt_writer_begin(&w, out, cap, version, ECMG_MSG_STREAM_STATUS);
  return simulcrypt_writer_finish(&w);
}

static size_t fake_build_channel_error_bad_version(unsigned char *out, size_t cap, unsigned char version) {
  simulcrypt_writer_t w;
  unsigned char err[2];
  err[0] = (unsigned char)(ECMG_ERR_UNSUPPORTED_PROTOCOL_VERSION >> 8);
  err[1] = (unsigned char)ECMG_ERR_UNSUPPORTED_PROTOCOL_VERSION;
  simulcrypt_writer_begin(&w, out, cap, version, ECMG_MSG_CHANNEL_ERROR);
  simulcrypt_writer_put_tlv(&w, ECMG_P_ERROR_STATUS, err, 2);
  return simulcrypt_writer_finish(&w);
}

static size_t fake_build_ecm_response(unsigned char *out, size_t cap, unsigned char version, unsigned short cp_number) {
  simulcrypt_writer_t w;
  unsigned char fake_ecm[] = {0x80, 0x70, 0x05, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
  fake_ecm[3] = (unsigned char)(cp_number >> 8);
  fake_ecm[4] = (unsigned char)cp_number;
  simulcrypt_writer_begin(&w, out, cap, version, ECMG_MSG_ECM_RESPONSE);
  simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_DATAGRAM, fake_ecm, sizeof fake_ecm);
  return simulcrypt_writer_finish(&w);
}

static void *fake_ecmg_thread(void *arg) {
  fake_ecmg_t *fe = arg;

  while (!atomic_load_explicit(&fe->stop, memory_order_relaxed)) {
    struct pollfd pfd;
    int pr, fd;
    simulcrypt_reader_t rd;
    simulcrypt_hdr_t hdr;
    const unsigned char *payload;
    unsigned char msg[512];
    size_t len;
    unsigned char version;

    pfd.fd = fe->listen_fd;
    pfd.events = POLLIN;
    pr = poll(&pfd, 1, 150);
    if (pr <= 0)
      continue;
    fd = accept(fe->listen_fd, NULL, NULL);
    if (fd < 0)
      continue;
    atomic_fetch_add_explicit(&fe->connections_seen, 1, memory_order_relaxed);
    simulcrypt_reader_init(&rd);

    if (simulcrypt_reader_poll(&rd, fd, 3000, &hdr, &payload) != 1 || hdr.type != ECMG_MSG_CHANNEL_SETUP) {
      close(fd);
      continue;
    }
    version = hdr.version;
    if (atomic_load_explicit(&fe->reject_first_version, memory_order_relaxed)) {
      atomic_store_explicit(&fe->reject_first_version, 0, memory_order_relaxed);
      len = fake_build_channel_error_bad_version(msg, sizeof msg, version);
      simulcrypt_send_all(fd, msg, len, 3000);
      close(fd);
      continue; /* client retries on a fresh connection with version_min */
    }
    len = fake_build_channel_status(msg, sizeof msg, version);
    simulcrypt_send_all(fd, msg, len, 3000);

    if (simulcrypt_reader_poll(&rd, fd, 3000, &hdr, &payload) != 1 || hdr.type != ECMG_MSG_STREAM_SETUP) {
      close(fd);
      continue;
    }
    len = fake_build_stream_status(msg, sizeof msg, version);
    simulcrypt_send_all(fd, msg, len, 3000);

    while (!atomic_load_explicit(&fe->stop, memory_order_relaxed)) {
      int rc = simulcrypt_reader_poll(&rd, fd, 500, &hdr, &payload);
      if (rc < 0)
        break;
      if (rc == 0)
        continue;
      if (hdr.type != ECMG_MSG_CW_PROVISION)
        continue;
      {
        const unsigned char *cpv;
        unsigned short cpvlen, cp_number = 0;
        if (find_tlv(payload, hdr.payload_len, ECMG_P_CP_NUMBER, &cpv, &cpvlen) && cpvlen == 2)
          cp_number = (unsigned short)(((unsigned)cpv[0] << 8) | cpv[1]);
        len = fake_build_ecm_response(msg, sizeof msg, version, cp_number);
      }
      simulcrypt_send_all(fd, msg, len, 3000);
      if (atomic_load_explicit(&fe->close_after_first_ecm, memory_order_relaxed)) {
        atomic_store_explicit(&fe->close_after_first_ecm, 0, memory_order_relaxed);
        break;
      }
    }
    close(fd);
  }
  return NULL;
}

static void fake_ecmg_start(fake_ecmg_t *fe) {
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;

  memset(fe, 0, sizeof *fe);
  fe->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  ck_assert_int_ge(fe->listen_fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  bind(fe->listen_fd, (struct sockaddr *)&addr, sizeof addr);
  listen(fe->listen_fd, 4);
  getsockname(fe->listen_fd, (struct sockaddr *)&addr, &alen);
  fe->port = ntohs(addr.sin_port);
  pthread_create(&fe->thread, NULL, fake_ecmg_thread, fe);
}

static void fake_ecmg_stop(fake_ecmg_t *fe) {
  atomic_store_explicit(&fe->stop, 1, memory_order_relaxed);
  pthread_join(fe->thread, NULL);
  close(fe->listen_fd);
}

static int wait_for_connected_state(ecmg_client_t *c, int want, int timeout_ms) {
  int waited = 0;
  while (ecmg_client_connected(c) != want && waited < timeout_ms) {
    struct timespec ts = {0, 10L * 1000000L};
    nanosleep(&ts, NULL);
    waited += 10;
  }
  return ecmg_client_connected(c) == want;
}

static int wait_for_connections_seen(fake_ecmg_t *fe, int want, int timeout_ms) {
  int waited = 0;
  while (atomic_load_explicit(&fe->connections_seen, memory_order_relaxed) < want && waited < timeout_ms) {
    struct timespec ts = {0, 10L * 1000000L};
    nanosleep(&ts, NULL);
    waited += 10;
  }
  return atomic_load_explicit(&fe->connections_seen, memory_order_relaxed) >= want;
}

/* passive: no counter bump. caller crosses exactly one boundary first, via
   wait_for_connected_state(c,1,...) then one atomic_fetch_add of packets_per_cp */
static int wait_for_epoch_above(ecmg_client_t *c, unsigned long floor, int timeout_ms) {
  int waited = 0;
  while (ecmg_client_ecm_epoch(c) <= floor && waited < timeout_ms) {
    struct timespec ts = {0, 10L * 1000000L};
    nanosleep(&ts, NULL);
    waited += 10;
  }
  return ecmg_client_ecm_epoch(c) > floor;
}

START_TEST(ecmg_client_completes_real_handshake_and_gets_ecm) {
  fake_ecmg_t fe;
  ecmg_client_cfg_t cfg;
  ecmg_client_t *c;
  atomic_ulong counter;
  unsigned char ecm[SIMULCRYPT_MAX_PAYLOAD];
  size_t ecm_len;

  fake_ecmg_start(&fe);
  atomic_init(&counter, 100); /* already past packets_per_cp: CW_provision fires immediately */

  memset(&cfg, 0, sizeof cfg);
  cfg.host = "127.0.0.1";
  cfg.port = fe.port;
  cfg.version_min = cfg.version_max = 3;
  cfg.super_cas_id = 0x4A750001;
  cfg.ecm_id = 1;
  cfg.cp_duration_ms = 1000;
  cfg.algo = SCRAMBLE_ALGO_CSA2;
  cfg.outage_mode = ECMG_OUTAGE_FROZEN;

  c = ecmg_client_start(&cfg, &counter, 5, 1);
  ck_assert_ptr_nonnull(c);
  ck_assert_int_eq(wait_for_connected_state(c, 1, 10000), 1);
  atomic_fetch_add_explicit(&counter, 5, memory_order_relaxed);
  ck_assert_int_eq(wait_for_epoch_above(c, 0, 10000), 1);
  ck_assert_int_eq(ecmg_client_get_ecm(c, ecm, sizeof ecm, &ecm_len), 0);
  ck_assert_uint_eq(ecm_len, 8u);
  ck_assert_uint_eq(((unsigned)ecm[3] << 8) | ecm[4], 1u);

  ecmg_client_stop(c);
  fake_ecmg_stop(&fe);
  ck_assert_int_eq(atomic_load_explicit(&fe.connections_seen, memory_order_relaxed), 1);
}
END_TEST

START_TEST(ecmg_client_falls_back_to_version_min_on_rejection) {
  fake_ecmg_t fe;
  ecmg_client_cfg_t cfg;
  ecmg_client_t *c;
  atomic_ulong counter;

  fake_ecmg_start(&fe);
  atomic_store_explicit(&fe.reject_first_version, 1, memory_order_relaxed);
  atomic_init(&counter, 100);

  memset(&cfg, 0, sizeof cfg);
  cfg.host = "127.0.0.1";
  cfg.port = fe.port;
  cfg.version_min = 2;
  cfg.version_max = 3;
  cfg.super_cas_id = 0x4A750001;
  cfg.ecm_id = 1;
  cfg.cp_duration_ms = 1000;
  cfg.algo = SCRAMBLE_ALGO_CSA2;
  cfg.outage_mode = ECMG_OUTAGE_FROZEN;

  c = ecmg_client_start(&cfg, &counter, 5, 1);
  ck_assert_ptr_nonnull(c);
  ck_assert_int_eq(wait_for_connected_state(c, 1, 10000), 1);
  atomic_fetch_add_explicit(&counter, 5, memory_order_relaxed);
  ck_assert_int_eq(wait_for_epoch_above(c, 0, 10000), 1);

  ecmg_client_stop(c);
  fake_ecmg_stop(&fe);
  /* rejected on the first connection (version 3), succeeded on a second (version_min=2) */
  ck_assert_int_eq(atomic_load_explicit(&fe.connections_seen, memory_order_relaxed), 2);
}
END_TEST

START_TEST(ecmg_client_reconnects_after_dropped_connection) {
  fake_ecmg_t fe;
  ecmg_client_cfg_t cfg;
  ecmg_client_t *c;
  atomic_ulong counter;
  unsigned long first_epoch;
  unsigned char ecm[SIMULCRYPT_MAX_PAYLOAD];
  size_t ecm_len;

  fake_ecmg_start(&fe);
  atomic_store_explicit(&fe.close_after_first_ecm, 1, memory_order_relaxed);
  atomic_init(&counter, 100);

  memset(&cfg, 0, sizeof cfg);
  cfg.host = "127.0.0.1";
  cfg.port = fe.port;
  cfg.version_min = cfg.version_max = 3;
  cfg.super_cas_id = 0x4A750001;
  cfg.ecm_id = 1;
  cfg.cp_duration_ms = 1000;
  cfg.algo = SCRAMBLE_ALGO_CSA2;
  cfg.outage_mode = ECMG_OUTAGE_FROZEN;

  c = ecmg_client_start(&cfg, &counter, 5, 1);
  ck_assert_ptr_nonnull(c);
  ck_assert_int_eq(wait_for_connected_state(c, 1, 10000), 1);
  atomic_fetch_add_explicit(&counter, 5, memory_order_relaxed);
  ck_assert_int_eq(wait_for_epoch_above(c, 0, 10000), 1);
  first_epoch = ecmg_client_ecm_epoch(c);

  ck_assert_int_eq(wait_for_connections_seen(&fe, 2, 10000), 1);
  ck_assert_int_eq(wait_for_connected_state(c, 1, 10000), 1);
  atomic_fetch_add_explicit(&counter, 5, memory_order_relaxed);
  ck_assert_int_eq(wait_for_epoch_above(c, first_epoch, 10000), 1);

  ck_assert_int_eq(ecmg_client_get_ecm(c, ecm, sizeof ecm, &ecm_len), 0);
  ck_assert_uint_eq(((unsigned)ecm[3] << 8) | ecm[4], 1u); /* fresh cp_number sequence again */

  ecmg_client_stop(c);
  fake_ecmg_stop(&fe);
  ck_assert_int_eq(atomic_load_explicit(&fe.connections_seen, memory_order_relaxed), 2);
}
END_TEST

START_TEST(ecmg_client_cycling_alternates_last_two_ecms) {
  fake_ecmg_t fe;
  ecmg_client_cfg_t cfg;
  ecmg_client_t *c;
  atomic_ulong counter;
  unsigned char ecm[SIMULCRYPT_MAX_PAYLOAD];
  size_t ecm_len;

  fake_ecmg_start(&fe);
  atomic_init(&counter, 100);

  memset(&cfg, 0, sizeof cfg);
  cfg.host = "127.0.0.1";
  cfg.port = fe.port;
  cfg.version_min = cfg.version_max = 3;
  cfg.super_cas_id = 0x4A750001;
  cfg.ecm_id = 1;
  cfg.cp_duration_ms = 1000;
  cfg.algo = SCRAMBLE_ALGO_CSA2;
  cfg.outage_mode = ECMG_OUTAGE_CYCLING;

  c = ecmg_client_start(&cfg, &counter, 5, 1);
  ck_assert_ptr_nonnull(c);
  ck_assert_int_eq(wait_for_connected_state(c, 1, 10000), 1);
  atomic_fetch_add_explicit(&counter, 5, memory_order_relaxed);
  ck_assert_int_eq(wait_for_epoch_above(c, 0, 10000), 1); /* cp=1 */
  atomic_fetch_add_explicit(&counter, 5, memory_order_relaxed);
  ck_assert_int_eq(wait_for_epoch_above(c, 1, 10000), 1); /* cp=2 */

  fake_ecmg_stop(&fe);
  ck_assert_int_eq(wait_for_connected_state(c, 0, 10000), 1);

  ck_assert_int_eq(ecmg_client_get_ecm(c, ecm, sizeof ecm, &ecm_len), 0);
  ck_assert_uint_eq(((unsigned)ecm[3] << 8) | ecm[4], 2u);

  atomic_fetch_add_explicit(&counter, 5, memory_order_relaxed);
  ck_assert_int_eq(ecmg_client_get_ecm(c, ecm, sizeof ecm, &ecm_len), 0);
  ck_assert_uint_eq(((unsigned)ecm[3] << 8) | ecm[4], 1u);

  atomic_fetch_add_explicit(&counter, 5, memory_order_relaxed);
  ck_assert_int_eq(ecmg_client_get_ecm(c, ecm, sizeof ecm, &ecm_len), 0);
  ck_assert_uint_eq(((unsigned)ecm[3] << 8) | ecm[4], 2u);

  ecmg_client_stop(c);
}
END_TEST

static Suite *ecmg_client_suite(void) {
  Suite *s = suite_create("ecmg_client");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, channel_setup_builds_expected_fields);
  tcase_add_test(tc, channel_setup_rejects_small_cap);
  tcase_add_test(tc, stream_setup_builds_expected_fields);
  tcase_add_test(tc, stream_setup_rejects_small_cap);
  tcase_add_test(tc, cw_provision_lead0_permsg1_sends_one_combo_at_cp);
  tcase_add_test(tc, cw_provision_lead1_permsg2_sends_current_and_next);
  tcase_add_test(tc, cw_provision_reuses_history_for_overlapping_cp);
  tcase_add_test(tc, cw_provision_rejects_small_cap);
  tcase_add_test(tc, cw_provision_encrypts_when_cwenc_active);
  tcase_add_test(tc, find_error_status_locates_tag);
  tcase_add_test(tc, find_error_status_absent_returns_error);
  tcase_add_test(tc, parse_channel_status_full_message);
  tcase_add_test(tc, parse_channel_status_missing_cw_per_msg_fails);
  tcase_add_test(tc, parse_channel_status_rejects_out_of_range_cw_per_msg);
  tcase_add_test(tc, ecm_available_frozen_always_available);
  tcase_add_test(tc, ecm_available_cycling_always_available);
  tcase_add_test(tc, ecm_available_silent_connected_is_available);
  tcase_add_test(tc, ecm_available_silent_disconnected_is_unavailable);
  tcase_add_test(tc, target_parity_frozen_ignores_elapsed_time);
  tcase_add_test(tc, target_parity_cycling_connected_uses_last_parity_directly);
  tcase_add_test(tc, target_parity_cycling_disconnected_flips_after_one_cp);
  tcase_add_test(tc, target_parity_cycling_disconnected_flips_back_after_two_cp);
  tcase_add_test(tc, target_parity_cycling_disconnected_within_cp_unchanged);
  suite_add_tcase(s, tc);

  {
    /* real sockets/reconnect timing: give this tcase more headroom than the default */
    TCase *tc_integ = tcase_create("integration");
    tcase_set_timeout(tc_integ, 60);
    tcase_add_test(tc_integ, ecmg_client_completes_real_handshake_and_gets_ecm);
    tcase_add_test(tc_integ, ecmg_client_falls_back_to_version_min_on_rejection);
    tcase_add_test(tc_integ, ecmg_client_reconnects_after_dropped_connection);
    tcase_add_test(tc_integ, ecmg_client_cycling_alternates_last_two_ecms);
    suite_add_tcase(s, tc_integ);
  }

  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ecmg_client_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
