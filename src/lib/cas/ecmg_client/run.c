/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lib/log.h"

#include "priv.h"

/* pre-seed hist[] from cw_source - cache-hit path, no self-gen.
   window: same first_cp calc as ecmg_build_cw_provision, exact match required. */
static int fill_hist_from_source(ecmg_client_t *c, cw_hist_entry_t *hist, unsigned short cp_number, unsigned lead_cw, unsigned cw_per_msg) {
  unsigned short first_cp = (unsigned short)(cp_number + lead_cw - cw_per_msg + 1);
  unsigned i;
  for (i = 0; i < cw_per_msg; i++) {
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

static void publish_cw(ecmg_client_t *c, int slot, const unsigned char *cw) {
  pthread_mutex_lock(&c->cw_lock);
  memcpy(c->cw_slot[slot], cw, c->cw_len);
  c->cw_slot_have[slot] = 1;
  pthread_mutex_unlock(&c->cw_lock);
  atomic_fetch_add_explicit(&c->cw_epoch, 1, memory_order_relaxed);
}

static void publish_ecm(ecmg_client_t *c, const unsigned char *dg, size_t dg_len) {
  pthread_mutex_lock(&c->ecm_lock);
  memcpy(c->ecm, dg, dg_len);
  c->ecm_len = dg_len;
  pthread_mutex_unlock(&c->ecm_lock);
  atomic_fetch_add_explicit(&c->ecm_epoch, 1, memory_order_relaxed);
}

/* run CW_provision cadence until disconnect/error. returns -1 to trigger reconn */
static int run_steady_state(ecmg_client_t *c, int fd, unsigned char version, unsigned lead_cw, unsigned cw_per_msg, unsigned max_comp_time_ms) {
  cw_hist_entry_t hist[ECMG_CW_HIST];
  simulcrypt_reader_t rd;
  unsigned short cp_number = 0;
  unsigned long next_boundary;

  memset(hist, 0, sizeof hist);
  simulcrypt_reader_init(&rd);
  next_boundary = atomic_load_explicit(c->packet_counter, memory_order_relaxed) + c->packets_per_cp;
  atomic_store_explicit(&c->connected, 1, memory_order_relaxed);
  if (c->cfg.cw_source.on_connected)
    c->cfg.cw_source.on_connected(c->cfg.cw_source.ctx);

  while (!ecmg_stopping(c)) {
    unsigned long cur = atomic_load_explicit(c->packet_counter, memory_order_relaxed);
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
      len = ecmg_build_cw_provision(msg, sizeof msg, version, cp_number, hist, c->cw_len, lead_cw, cw_per_msg);
      if (!len) {
        log_line("ecmg: failed to build CW_provision");
        return -1;
      }
      publish_cw(c, cp_number & 1, hist[cp_number % ECMG_CW_HIST].cw);
      atomic_store_explicit(&c->cw_published_at, cur, memory_order_relaxed);

      if (simulcrypt_send_all(fd, msg, len, ECMG_HANDSHAKE_TIMEOUT_MS) < 0) {
        log_line("ecmg: CW_provision send failed");
        atomic_fetch_add_explicit(&c->ecm_errors_total, 1, memory_order_relaxed);
        return -1;
      }
      if (wait_for_message(c, &rd, fd, (int)max_comp_time_ms + ECMG_HANDSHAKE_TIMEOUT_MS, &hdr, &payload) != 1) {
        if (!ecmg_stopping(c))
          log_line("ecmg: no ECM_response for CP %u", cp_number);
        atomic_fetch_add_explicit(&c->ecm_errors_total, 1, memory_order_relaxed);
        return -1;
      }
      if (hdr.type == ECMG_MSG_ECM_RESPONSE) {
        simulcrypt_tlv_reader_t it;
        unsigned short tag, vlen;
        const unsigned char *val;
        simulcrypt_tlv_reader_init(&it, payload, hdr.payload_len);
        while (simulcrypt_tlv_reader_next(&it, &tag, &val, &vlen) == 1) {
          if (tag == ECMG_P_ECM_DATAGRAM) {
            publish_ecm(c, val, vlen);
            atomic_fetch_add_explicit(&c->ecm_total, 1, memory_order_relaxed);
            break;
          }
        }
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
      if (rc == 1 && (hdr.type == ECMG_MSG_CHANNEL_ERROR || hdr.type == ECMG_MSG_STREAM_ERROR)) {
        unsigned short err = 0;
        ecmg_find_error_status(payload, hdr.payload_len, &err);
        log_line("ecmg: async error 0x%04x, error_status=0x%04x", hdr.type, err);
        return -1;
      }
    }
  }
  return 0;
}

/* backoff in small steps so a 30s cap doesn't delay shutdown by 30s */
static void interruptible_backoff(ecmg_client_t *c, unsigned ms) {
  unsigned waited = 0;
  while (waited < ms && !ecmg_stopping(c)) {
    unsigned step = (ms - waited < ECMG_POLL_INTERVAL_MS) ? ms - waited : ECMG_POLL_INTERVAL_MS;
    struct timespec ts = {step / 1000, (long)(step % 1000) * 1000000L};
    nanosleep(&ts, NULL);
    waited += step;
  }
}

void *ecmg_client_main(void *arg) {
  ecmg_client_t *c = arg;
  unsigned backoff_ms = ECMG_RECONNECT_BACKOFF_MIN_MS;

  while (!ecmg_stopping(c)) {
    int fd;
    unsigned char version;
    unsigned lead_cw, cw_per_msg, max_comp_time_ms;

    if (connect_and_setup(c, &fd, &version, &lead_cw, &cw_per_msg, &max_comp_time_ms) < 0) {
      interruptible_backoff(c, backoff_ms);
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
