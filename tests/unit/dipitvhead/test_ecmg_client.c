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

#include "dipitvhead/cas/ecmg_client.h"
#include "dipitvhead/cas/simulcrypt_msg.h"

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

  memset(hist, 0, sizeof hist);
  size_t n = ecmg_build_cw_provision(buf, sizeof buf, 3, 500, hist, 8, 0, 1);
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

  memset(hist, 0, sizeof hist);
  size_t n = ecmg_build_cw_provision(buf, sizeof buf, 3, 500, hist, 16, 1, 2);
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

  memset(hist, 0, sizeof hist);
  /* CP=500: combo for [500,501] - caches CW(501) */
  size_t n1 = ecmg_build_cw_provision(buf1, sizeof buf1, 3, 500, hist, 16, 1, 2);
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
  size_t n2 = ecmg_build_cw_provision(buf2, sizeof buf2, 3, 501, hist, 16, 1, 2);
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
  memset(hist, 0, sizeof hist);
  ck_assert_uint_eq(ecmg_build_cw_provision(buf, sizeof buf, 3, 1, hist, 16, 0, 1), 0u);
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

START_TEST(cw_valid_frozen_always_valid) {
  ck_assert_int_eq(ecmg_cw_valid_calc(ECMG_RESILIENCE_FROZEN, 0, 10, 1000, 0), 1);
}
END_TEST

START_TEST(cw_valid_cycling_always_valid) {
  ck_assert_int_eq(ecmg_cw_valid_calc(ECMG_RESILIENCE_CYCLING, 0, 10, 1000, 0), 1);
}
END_TEST

START_TEST(cw_valid_unscrambled_connected_is_valid) {
  ck_assert_int_eq(ecmg_cw_valid_calc(ECMG_RESILIENCE_UNSCRAMBLED, 1, 10, 1000, 0), 1);
}
END_TEST

START_TEST(cw_valid_unscrambled_within_one_cp_is_valid) {
  ck_assert_int_eq(ecmg_cw_valid_calc(ECMG_RESILIENCE_UNSCRAMBLED, 0, 10, 109, 100), 1);
}
END_TEST

START_TEST(cw_valid_unscrambled_past_one_cp_is_invalid) {
  ck_assert_int_eq(ecmg_cw_valid_calc(ECMG_RESILIENCE_UNSCRAMBLED, 0, 10, 110, 100), 0);
}
END_TEST

START_TEST(cw_valid_unscrambled_no_cadence_is_valid) {
  ck_assert_int_eq(ecmg_cw_valid_calc(ECMG_RESILIENCE_UNSCRAMBLED, 0, 0, 100000, 0), 1);
}
END_TEST

START_TEST(target_parity_frozen_ignores_elapsed_time) {
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_RESILIENCE_FROZEN, 0, 10, 1000, 0, 5), 1);
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_RESILIENCE_FROZEN, 0, 10, 1000, 0, 4), 0);
}
END_TEST

START_TEST(target_parity_cycling_connected_uses_epoch_directly) {
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_RESILIENCE_CYCLING, 1, 10, 100000, 0, 3), 1);
}
END_TEST

START_TEST(target_parity_cycling_disconnected_flips_after_one_cp) {
  /* epoch=3 (odd/1), one whole CP elapsed since publish -> flips to even/0 */
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_RESILIENCE_CYCLING, 0, 10, 110, 100, 3), 0);
}
END_TEST

START_TEST(target_parity_cycling_disconnected_flips_back_after_two_cp) {
  /* two whole CPs elapsed -> back to the original parity */
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_RESILIENCE_CYCLING, 0, 10, 120, 100, 3), 1);
}
END_TEST

START_TEST(target_parity_cycling_disconnected_within_cp_unchanged) {
  ck_assert_int_eq(ecmg_target_parity_calc(ECMG_RESILIENCE_CYCLING, 0, 10, 105, 100, 3), 1);
}
END_TEST

/* below: standalone integration tests. no swampcastle - a small fake ECMG server
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

static size_t fake_build_ecm_response(unsigned char *out, size_t cap, unsigned char version) {
  simulcrypt_writer_t w;
  static const unsigned char fake_ecm[] = {0x80, 0x70, 0x05, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
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
      len = fake_build_ecm_response(msg, sizeof msg, version);
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

/* next_boundary is computed relative to *counter at connect time*, not zero, so a one-shot
   bump before ecmg_client_start() doesn't reliably cross it - keep advancing it while waiting */
static int wait_for_epoch_above(ecmg_client_t *c, atomic_ulong *counter, unsigned long floor, int timeout_ms) {
  int waited = 0;
  while (waited < timeout_ms) {
    struct timespec ts = {0, 50L * 1000000L};
    if (ecmg_client_cw_epoch(c) > floor)
      return 1;
    atomic_fetch_add_explicit(counter, 1000, memory_order_relaxed);
    nanosleep(&ts, NULL);
    waited += 50;
  }
  return ecmg_client_cw_epoch(c) > floor;
}

START_TEST(ecmg_client_completes_real_handshake_and_gets_cw) {
  fake_ecmg_t fe;
  ecmg_client_cfg_t cfg;
  ecmg_client_t *c;
  atomic_ulong counter;
  unsigned char cw[ECMG_MAX_CW_LEN];
  size_t cw_len;

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
  cfg.resilience = ECMG_RESILIENCE_FROZEN;

  c = ecmg_client_start(&cfg, &counter, 5, 1);
  ck_assert_ptr_nonnull(c);
  ck_assert_int_eq(wait_for_epoch_above(c, &counter, 0, 4000), 1);
  ck_assert_int_eq(ecmg_client_get_cw(c, (int)(ecmg_client_cw_epoch(c) & 1UL), cw, sizeof cw, &cw_len), 0);
  ck_assert_uint_eq(cw_len, 8); /* CSA2 */

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
  cfg.resilience = ECMG_RESILIENCE_FROZEN;

  c = ecmg_client_start(&cfg, &counter, 5, 1);
  ck_assert_ptr_nonnull(c);
  ck_assert_int_eq(wait_for_epoch_above(c, &counter, 0, 4000), 1);

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
  cfg.resilience = ECMG_RESILIENCE_FROZEN;

  c = ecmg_client_start(&cfg, &counter, 5, 1);
  ck_assert_ptr_nonnull(c);
  ck_assert_int_eq(wait_for_epoch_above(c, &counter, 0, 4000), 1);
  first_epoch = ecmg_client_cw_epoch(c);

  /* fake server dropped the connection after replying once; bump the counter again so the
     reconnected client has a fresh CP boundary to react to, then wait for a second CW */
  atomic_fetch_add_explicit(&counter, 100, memory_order_relaxed);
  ck_assert_int_eq(wait_for_epoch_above(c, &counter, first_epoch, 4000), 1);

  ecmg_client_stop(c);
  fake_ecmg_stop(&fe);
  ck_assert_int_eq(atomic_load_explicit(&fe.connections_seen, memory_order_relaxed), 2);
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
  tcase_add_test(tc, find_error_status_locates_tag);
  tcase_add_test(tc, find_error_status_absent_returns_error);
  tcase_add_test(tc, parse_channel_status_full_message);
  tcase_add_test(tc, parse_channel_status_missing_cw_per_msg_fails);
  tcase_add_test(tc, parse_channel_status_rejects_out_of_range_cw_per_msg);
  tcase_add_test(tc, cw_valid_frozen_always_valid);
  tcase_add_test(tc, cw_valid_cycling_always_valid);
  tcase_add_test(tc, cw_valid_unscrambled_connected_is_valid);
  tcase_add_test(tc, cw_valid_unscrambled_within_one_cp_is_valid);
  tcase_add_test(tc, cw_valid_unscrambled_past_one_cp_is_invalid);
  tcase_add_test(tc, cw_valid_unscrambled_no_cadence_is_valid);
  tcase_add_test(tc, target_parity_frozen_ignores_elapsed_time);
  tcase_add_test(tc, target_parity_cycling_connected_uses_epoch_directly);
  tcase_add_test(tc, target_parity_cycling_disconnected_flips_after_one_cp);
  tcase_add_test(tc, target_parity_cycling_disconnected_flips_back_after_two_cp);
  tcase_add_test(tc, target_parity_cycling_disconnected_within_cp_unchanged);
  suite_add_tcase(s, tc);

  {
    /* real sockets/reconnect timing: give this tcase more headroom than the default */
    TCase *tc_integ = tcase_create("integration");
    tcase_set_timeout(tc_integ, 15);
    tcase_add_test(tc_integ, ecmg_client_completes_real_handshake_and_gets_cw);
    tcase_add_test(tc_integ, ecmg_client_falls_back_to_version_min_on_rejection);
    tcase_add_test(tc_integ, ecmg_client_reconnects_after_dropped_connection);
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
