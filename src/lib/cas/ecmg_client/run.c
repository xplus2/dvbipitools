/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>
#include <unistd.h>

#include "lib/cas/cas_dial.h"
#include "lib/helper/log.h"
#include "lib/helper/secure_zero.h"
#include "lib/sys/ioutil.h"

#include "priv.h"

/* pre-seed hist[] from cw_source - cache-hit path, no self-gen.
   window: same first_cp calc as ecmg_build_cw_provision, exact match required. */
static int fill_hist_from_source(ecmg_client_t *c, cw_hist_entry_t *hist, unsigned short cp_number, unsigned lead_cw, unsigned cw_per_msg) {
  unsigned short first_cp = (unsigned short)(cp_number + lead_cw - cw_per_msg + 1);
  for (unsigned i = 0; i < cw_per_msg; i++) {
    unsigned short cp = (unsigned short)(first_cp + i);
    int idx = cp % ECMG_CW_HIST;
    if (hist[idx].valid && hist[idx].cp_number == cp)
      continue;
    if (c->cfg.cw_source.get_cw(c->cfg.cw_source.ctx, cp, hist[idx].cw, c->cw_len) < 0)
      return -1;
    hist[idx].cp_number = cp;
    hist[idx].valid = 1;
  }
  return 0;
}

static void publish_ecm(ecmg_client_t *c, int slot, const unsigned char *dg, size_t dg_len) {
  pthread_mutex_lock(&c->ecm_lock);
  memcpy(c->ecm_slot[slot], dg, dg_len);
  c->ecm_slot_len[slot] = dg_len;
  pthread_mutex_unlock(&c->ecm_lock);
  atomic_fetch_add_explicit(&c->ecm_epoch, 1, memory_order_relaxed);
  atomic_store_explicit(&c->last_parity, slot, memory_order_relaxed);
}

static void log_ecm_wait_failed(ecmg_client_t *c, unsigned short cp_number) {
  if (!ecmg_stopping(c))
    log_line("ecmg: no ECM_response for CP %u", cp_number);
}

static void publish_ecm_response(ecmg_client_t *c, unsigned short cp_number, const unsigned char *payload, unsigned short payload_len) {
  simulcrypt_tlv_reader_t it;
  unsigned short tag, vlen;
  const unsigned char *val;
  simulcrypt_tlv_reader_init(&it, payload, payload_len);
  while (simulcrypt_tlv_reader_next(&it, &tag, &val, &vlen) == 1) {
    if (tag == ECMG_P_ECM_DATAGRAM) {
      publish_ecm(c, cp_number & 1, val, vlen); /* TS 103 197 clause 5.3: CP_number parity == CW parity */
      atomic_fetch_add_explicit(&c->ecm_total, 1, memory_order_relaxed);
      break;
    }
  }
}

typedef struct {
  ecmg_client_t *c;
  int fd;
  unsigned char version;
  unsigned lead_cw, cw_per_msg, max_comp_time_ms;
  int64_t next_test_at;
  int64_t test_deadline; /* 0 = no channel_test outstanding */
} ecmg_session_t;

static int session_send(ecmg_session_t *s, const unsigned char *msg, size_t len) {
  if (!len || simulcrypt_send_all(s->fd, msg, len, ECMG_HANDSHAKE_TIMEOUT_MS) < 0) {
    log_line("ecmg: reply send failed");
    return -1;
  }
  return 0;
}

/* periodic channel_test, fails when too late */
static int session_tick(ecmg_session_t *s) {
  unsigned char msg[SIMULCRYPT_HDR_LEN + 16];
  int64_t now = now_ms();
  if (s->test_deadline) {
    if (now < s->test_deadline) return 0;
    log_line("ecmg: no channel_status reply to channel_test");
    return -1;
  }
  if (now < s->next_test_at) return 0;
  s->next_test_at = now + ECMG_CHANNEL_TEST_INTERVAL_MS;
  s->test_deadline = now + ECMG_HANDSHAKE_TIMEOUT_MS;
  return session_send(s, msg, ecmg_build_channel_test(msg, sizeof msg, s->version));
}

/* 0 consumed, -1 fatal, 1 no session msg */
static int session_handle(ecmg_session_t *s, const simulcrypt_hdr_t *hdr, const unsigned char *payload) {
  unsigned char msg[SIMULCRYPT_HDR_LEN + 64];
  switch (hdr->type) {
    case ECMG_MSG_CHANNEL_TEST:
      return session_send(s, msg, ecmg_build_channel_status(msg, sizeof msg, s->version, s->lead_cw, s->cw_per_msg, s->max_comp_time_ms, atomic_load_explicit(&s->c->ecm_rep_period_ms, memory_order_relaxed)));
    case ECMG_MSG_STREAM_TEST:
      return session_send(s, msg, ecmg_build_stream_status(msg, sizeof msg, s->version, s->c->cfg.ecm_id));
    case ECMG_MSG_CHANNEL_STATUS:
      s->test_deadline = 0;
      return 0;
    case ECMG_MSG_STREAM_STATUS:
      return 0;
    case ECMG_MSG_CHANNEL_ERROR:
    case ECMG_MSG_STREAM_ERROR: {
      unsigned short err = 0;
      ecmg_find_error_status(payload, hdr->payload_len, &err);
      log_line("ecmg: async error 0x%04x, error_status=0x%04x", hdr->type, err);
      return -1;
    }
    default:
      return 1;
  }
}

/* 1 non-session msg in hdr/payload, 0 timeout or stop, -1 lost or err */
static int session_wait_reply(ecmg_session_t *s, simulcrypt_reader_t *rd, int timeout_ms, simulcrypt_hdr_t *hdr, const unsigned char **payload) {
  int64_t deadline = now_ms() + timeout_ms;
  for (;;) {
    int64_t left = deadline - now_ms();
    int rc;
    if (left <= 0) return 0;
    rc = wait_for_message(s->c, rd, s->fd, (int)left, hdr, payload);
    if (rc != 1) return rc;
    rc = session_handle(s, hdr, *payload);
    if (rc != 0) return rc < 0 ? -1 : 1;
  }
}

static int run_cp_loop(ecmg_client_t *c, int fd, unsigned char version, unsigned lead_cw, unsigned cw_per_msg, unsigned max_comp_time_ms, cw_hist_entry_t *hist) {
  ecmg_session_t sess = {.c = c, .fd = fd, .version = version, .lead_cw = lead_cw, .cw_per_msg = cw_per_msg, .max_comp_time_ms = max_comp_time_ms, .next_test_at = now_ms() + ECMG_CHANNEL_TEST_INTERVAL_MS};
  simulcrypt_reader_t rd;
  unsigned short cp_number = 0;
  unsigned long next_boundary;

  simulcrypt_reader_init(&rd);
  next_boundary = atomic_load_explicit(c->packet_counter, memory_order_relaxed) + c->packets_per_cp;
  atomic_store_explicit(&c->connected, 1, memory_order_relaxed);
  if (c->cfg.cw_source.on_connected) cp_number = c->cfg.cw_source.on_connected(c->cfg.cw_source.ctx);

  while (!ecmg_stopping(c)) {
    unsigned long cur = atomic_load_explicit(c->packet_counter, memory_order_relaxed);
    if (session_tick(&sess) < 0) return -1;
    if (c->packets_per_cp == 0 || cur + c->lookahead_margin_packets >= next_boundary) {
      unsigned char msg[SIMULCRYPT_MAX_FRAME];
      simulcrypt_hdr_t hdr;
      const unsigned char *payload;
      size_t len;

      cp_number++;
      atomic_fetch_add_explicit(&c->cryptoperiod_transitions_total, 1, memory_order_relaxed);
      if (c->cfg.cw_source.get_cw && fill_hist_from_source(c, hist, cp_number, lead_cw, cw_per_msg) < 0) {
        log_line("ecmg: CW source failed for CP %u", cp_number);
        return -1;
      }
      len = ecmg_build_cw_provision(msg, sizeof msg, version, cp_number, hist, c->cw_len, lead_cw, cw_per_msg, &c->cwenc_ctx);
      if (!len) {
        log_line("ecmg: failed to build CW_provision");
        return -1;
      }
      atomic_store_explicit(&c->cw_published_at, cur, memory_order_relaxed);

      if (simulcrypt_send_all(fd, msg, len, ECMG_HANDSHAKE_TIMEOUT_MS) < 0) {
        log_line("ecmg: CW_provision send failed");
        atomic_fetch_add_explicit(&c->ecm_errors_total, 1, memory_order_relaxed);
        return -1;
      }
      if (session_wait_reply(&sess, &rd, (int)max_comp_time_ms + ECMG_HANDSHAKE_TIMEOUT_MS, &hdr, &payload) != 1) {
        log_ecm_wait_failed(c, cp_number);
        atomic_fetch_add_explicit(&c->ecm_errors_total, 1, memory_order_relaxed);
        return -1;
      }
      if (hdr.type == ECMG_MSG_ECM_RESPONSE) {
        publish_ecm_response(c, cp_number, payload, hdr.payload_len);
      } else {
        unsigned short err = 0;
        ecmg_find_error_status(payload, hdr.payload_len, &err);
        log_line("ecmg: CW_provision rejected, reply=0x%04x error_status=0x%04x", hdr.type, err);
        atomic_fetch_add_explicit(&c->ecm_errors_total, 1, memory_order_relaxed);
        return -1;
      }
      next_boundary += c->packets_per_cp;
    } else {
      simulcrypt_hdr_t hdr;
      const unsigned char *payload;
      int rc = simulcrypt_reader_poll(&rd, fd, ECMG_POLL_INTERVAL_MS, &hdr, &payload);
      if (rc < 0) {
        log_line("ecmg: connection lost");
        return -1;
      }
      if (rc == 1 && session_handle(&sess, &hdr, payload) < 0)
        return -1;
    }
  }
  return 0;
}

static int run_steady_state(ecmg_client_t *c, int fd, unsigned char version, unsigned lead_cw, unsigned cw_per_msg, unsigned max_comp_time_ms) {
  cw_hist_entry_t hist[ECMG_CW_HIST];
  int rc;
  memset(hist, 0, sizeof hist);
  rc = run_cp_loop(c, fd, version, lead_cw, cw_per_msg, max_comp_time_ms, hist);
  secure_zero(hist, sizeof hist);
  return rc;
}

void *ecmg_client_main(void *arg) {
  ecmg_client_t *c = arg;
  unsigned backoff_ms = ECMG_RECONNECT_BACKOFF_MIN_MS;

  while (!ecmg_stopping(c)) {
    int fd;
    unsigned char version;
    unsigned lead_cw, cw_per_msg, max_comp_time_ms;

    if (connect_and_setup(c, &fd, &version, &lead_cw, &cw_per_msg, &max_comp_time_ms) < 0) {
      cas_interruptible_backoff(&c->stop, backoff_ms, ECMG_POLL_INTERVAL_MS);
      if (backoff_ms < ECMG_RECONNECT_BACKOFF_MAX_MS)
        backoff_ms *= 2;
      continue;
    }
    backoff_ms = ECMG_RECONNECT_BACKOFF_MIN_MS;

    run_steady_state(c, fd, version, lead_cw, cw_per_msg, max_comp_time_ms);
    atomic_store_explicit(&c->connected, 0, memory_order_relaxed);
    close(fd);
  }
  return NULL;
}
