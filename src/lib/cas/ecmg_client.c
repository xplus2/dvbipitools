/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/log.h"
#include "lib/mux/psi_build.h"
#include "lib/signal.h"

#include "ecmg_client.h"
#include "simulcrypt_msg.h"

#define ECMG_HANDSHAKE_TIMEOUT_MS 3000
#define ECMG_POLL_INTERVAL_MS 150
#define ECMG_RECONNECT_BACKOFF_MIN_MS 500
#define ECMG_RECONNECT_BACKOFF_MAX_MS 30000

struct ecmg_client {
  ecmg_client_cfg_t cfg;
  size_t cw_len;

  const atomic_ulong *packet_counter;
  unsigned long packets_per_cp;
  unsigned long lookahead_margin_packets;

  pthread_t thread;
  atomic_int stop;

  pthread_mutex_t cw_lock;
  unsigned char cw_slot[2][ECMG_MAX_CW_LEN];
  int cw_slot_have[2];
  atomic_ulong cw_epoch;

  atomic_int connected; /* 1 while a CW_provision/ECM_response cycle is live */
  atomic_ulong cw_published_at; /* packet_counter value when the cached CW was last (re)published */

  pthread_mutex_t ecm_lock;
  unsigned char ecm[SIMULCRYPT_MAX_PAYLOAD];
  size_t ecm_len;
  atomic_ulong ecm_epoch;

  atomic_uint ecm_rep_period_ms;

  atomic_ulong cryptoperiod_transitions_total;
  atomic_ulong ecm_total;
  atomic_ulong ecm_errors_total;
};

static int cw_gen(unsigned char *out, size_t len) {
  size_t got = 0;
  while (got < len) {
    ssize_t n = getrandom(out + got, len - got, 0);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      log_line("ecmg: getrandom: %s", strerror(errno));
      return -1;
    }
    got += (size_t)n;
  }
  return 0;
}

static const unsigned char *hist_get_or_gen(cw_hist_entry_t *hist, unsigned short cp, size_t cw_len) {
  int i = cp % ECMG_CW_HIST;
  if (hist[i].valid && hist[i].cp_number == cp)
    return hist[i].cw;
  if (cw_gen(hist[i].cw, cw_len) < 0)
    return NULL;
  hist[i].cp_number = cp;
  hist[i].valid = 1;
  return hist[i].cw;
}

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

int ecmg_client_get_cw(ecmg_client_t *c, int slot, unsigned char *cw_out, size_t cw_cap, size_t *cw_len_out) {
  int have;
  if (slot != 0 && slot != 1)
    return -1;
  pthread_mutex_lock(&c->cw_lock);
  have = c->cw_slot_have[slot];
  if (have) {
    if (cw_cap < c->cw_len) {
      pthread_mutex_unlock(&c->cw_lock);
      return -1;
    }
    memcpy(cw_out, c->cw_slot[slot], c->cw_len);
    *cw_len_out = c->cw_len;
  }
  pthread_mutex_unlock(&c->cw_lock);
  return have ? 0 : -1;
}

unsigned long ecmg_client_cw_epoch(ecmg_client_t *c) {
  return atomic_load_explicit(&c->cw_epoch, memory_order_relaxed);
}

int ecmg_ecm_available_calc(ecmg_outage_mode_t outage_mode, int connected) {
  return !(outage_mode == ECMG_OUTAGE_SILENT && !connected);
}

int ecmg_client_get_ecm(ecmg_client_t *c, unsigned char *out, size_t cap, size_t *len_out) {
  int have;
  if (!ecmg_ecm_available_calc(c->cfg.outage_mode, atomic_load_explicit(&c->connected, memory_order_relaxed)))
    return -1;
  pthread_mutex_lock(&c->ecm_lock);
  have = c->ecm_len > 0;
  if (have) {
    if (cap < c->ecm_len) {
      pthread_mutex_unlock(&c->ecm_lock);
      return -1;
    }
    memcpy(out, c->ecm, c->ecm_len);
    *len_out = c->ecm_len;
  }
  pthread_mutex_unlock(&c->ecm_lock);
  return have ? 0 : -1;
}

unsigned long ecmg_client_ecm_epoch(ecmg_client_t *c) {
  return atomic_load_explicit(&c->ecm_epoch, memory_order_relaxed);
}

unsigned ecmg_client_ecm_rep_period_ms(ecmg_client_t *c) {
  return atomic_load_explicit(&c->ecm_rep_period_ms, memory_order_relaxed);
}

int ecmg_target_parity_calc(ecmg_outage_mode_t outage_mode, int connected, unsigned long packets_per_cp, unsigned long cur, unsigned long published_at, unsigned long epoch) {
  unsigned long periods_elapsed;
  if (outage_mode != ECMG_OUTAGE_CYCLING)
    return (int)(epoch & 1UL);
  if (connected)
    return (int)(epoch & 1UL);
  if (packets_per_cp == 0)
    return (int)(epoch & 1UL);
  periods_elapsed = (cur - published_at) / packets_per_cp;
  return (int)((epoch + periods_elapsed) & 1UL);
}

int ecmg_client_connected(ecmg_client_t *c) { return atomic_load_explicit(&c->connected, memory_order_relaxed); }
unsigned long ecmg_client_cryptoperiod_transitions(ecmg_client_t *c) { return atomic_load_explicit(&c->cryptoperiod_transitions_total, memory_order_relaxed); }
unsigned long ecmg_client_ecm_total(ecmg_client_t *c) { return atomic_load_explicit(&c->ecm_total, memory_order_relaxed); }
unsigned long ecmg_client_ecm_errors(ecmg_client_t *c) { return atomic_load_explicit(&c->ecm_errors_total, memory_order_relaxed); }

/* frozen: always the last published parity. cycling, while disconnected: keeps alternating
   on the normal crypto-period schedule between the two last-known CWs, computed from how many
   whole periods have elapsed since the last publish. no waiting on the ECMG to come back.
   uses ecm_epoch, not cw_epoch: cw_epoch bumps the instant a CW_provision is sent, before the
   ECMG round trip; switching live scrambling on that would flip parity before the matching ECM
   is even on the wire. ecm_epoch only bumps once the ECM_response actually arrived. */
int ecmg_client_target_parity(ecmg_client_t *c) {
  unsigned long epoch = atomic_load_explicit(&c->ecm_epoch, memory_order_relaxed);
  unsigned long cur = atomic_load_explicit(c->packet_counter, memory_order_relaxed);
  unsigned long published_at = atomic_load_explicit(&c->cw_published_at, memory_order_relaxed);
  int connected = atomic_load_explicit(&c->connected, memory_order_relaxed);
  return ecmg_target_parity_calc(c->cfg.outage_mode, connected, c->packets_per_cp, cur, published_at, epoch);
}

/* checked everywhere a network wait could otherwise block shutdown past its own timeout */
static int ecmg_stopping(const ecmg_client_t *c) {
  return atomic_load_explicit(&c->stop, memory_order_relaxed) || signal_stop_requested();
}

/* 1 connected, -1 refused/error, 0 timed out or stop requested */
static int wait_connect(ecmg_client_t *c, int fd) {
  int elapsed = 0;
  while (elapsed < ECMG_HANDSHAKE_TIMEOUT_MS) {
    struct pollfd pfd;
    int step = ECMG_POLL_INTERVAL_MS;
    int pret;
    if (ecmg_stopping(c))
      return 0;
    if (step > ECMG_HANDSHAKE_TIMEOUT_MS - elapsed)
      step = ECMG_HANDSHAKE_TIMEOUT_MS - elapsed;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    pret = poll(&pfd, 1, step);
    if (pret > 0) {
      int soerr = 0;
      socklen_t sl = sizeof soerr;
      getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
      if (soerr == 0)
        return 1;
      errno = soerr;
      return -1;
    }
    if (pret < 0 && errno != EINTR)
      return -1;
    elapsed += step;
  }
  return 0;
}

/* nonblocking connect, poll-with-timeout: a blocking connect() to an unreachable
   host can hang for minutes at the OS level, blocking pthread_join() at shutdown */
static int tcp_dial(ecmg_client_t *c, const char *host, unsigned port) {
  struct addrinfo hints, *res, *ai;
  char portstr[6];
  int fd = -1, e, save_errno = 0;

  snprintf(portstr, sizeof portstr, "%u", port);
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  e = getaddrinfo(host, portstr, &hints, &res);
  if (e) {
    log_line("ecmg: resolve %s: %s", host, gai_strerror(e));
    return -1;
  }
  for (ai = res; ai; ai = ai->ai_next) {
    int flags, cr;
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0)
      continue;
    flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
      break;
    if (errno != EINPROGRESS) {
      save_errno = errno;
      close(fd);
      fd = -1;
      continue;
    }
    cr = wait_connect(c, fd);
    if (cr == 1)
      break;
    save_errno = (cr == -1) ? errno : ETIMEDOUT;
    close(fd);
    fd = -1;
    if (ecmg_stopping(c))
      break;
  }
  freeaddrinfo(res);
  if (fd < 0) {
    if (!ecmg_stopping(c))
      log_line("ecmg: connect %s:%u: %s", host, port, strerror(save_errno));
    return -1;
  }
  return fd; /* already O_NONBLOCK, set before connect() */
}

static int wait_for_message(ecmg_client_t *c, simulcrypt_reader_t *rd, int fd, int total_timeout_ms, simulcrypt_hdr_t *hdr, const unsigned char **payload) {
  int elapsed = 0;
  while (elapsed < total_timeout_ms) {
    int step = ECMG_POLL_INTERVAL_MS;
    int rc;
    if (ecmg_stopping(c))
      return 0;
    if (step > total_timeout_ms - elapsed)
      step = total_timeout_ms - elapsed;
    rc = simulcrypt_reader_poll(rd, fd, step, hdr, payload);
    if (rc != 0)
      return rc;
    elapsed += step;
  }
  return 0;
}

int ecmg_find_error_status(const unsigned char *body, size_t body_len, unsigned short *out) {
  simulcrypt_tlv_reader_t it;
  unsigned short tag, vlen;
  const unsigned char *val;
  int rc;
  simulcrypt_tlv_reader_init(&it, body, body_len);
  while ((rc = simulcrypt_tlv_reader_next(&it, &tag, &val, &vlen)) == 1) {
    if (tag == ECMG_P_ERROR_STATUS && vlen == 2) {
      *out = ((unsigned)val[0] << 8) | val[1];
      return 0;
    }
  }
  return -1;
}

/* 0 ok (fills all five), -1 malformed or cw_per_msg missing/out of range */
int ecmg_parse_channel_status(const unsigned char *body, size_t body_len, unsigned *out_lead_cw, unsigned *out_cw_per_msg, unsigned *out_max_comp_time_ms, unsigned *out_min_cp_100ms, unsigned *out_ecm_rep_period_ms) {
  unsigned lead_cw = 0, cw_per_msg = 0, max_comp_time_ms = 0, min_cp_100ms = 0, ecm_rep_period_ms = 0;
  simulcrypt_tlv_reader_t it;
  unsigned short tag, vlen;
  const unsigned char *val;
  int rc;
  simulcrypt_tlv_reader_init(&it, body, body_len);
  while ((rc = simulcrypt_tlv_reader_next(&it, &tag, &val, &vlen)) == 1) {
    switch (tag) {
      case ECMG_P_LEAD_CW:
        if (vlen == 1)
          lead_cw = val[0];
        break;
      case ECMG_P_CW_PER_MSG:
        if (vlen == 1)
          cw_per_msg = val[0];
        break;
      case ECMG_P_MAX_COMP_TIME:
        if (vlen == 2)
          max_comp_time_ms = ((unsigned)val[0] << 8) | val[1];
        break;
      case ECMG_P_MIN_CP_DURATION:
        if (vlen == 2)
          min_cp_100ms = ((unsigned)val[0] << 8) | val[1];
        break;
      case ECMG_P_ECM_REP_PERIOD:
        if (vlen == 2)
          ecm_rep_period_ms = ((unsigned)val[0] << 8) | val[1];
        break;
      default:
        break;
    }
  }
  if (rc < 0 || !cw_per_msg || cw_per_msg > ECMG_MAX_CW_PER_MSG)
    return -1;
  *out_lead_cw = lead_cw;
  *out_cw_per_msg = cw_per_msg;
  *out_max_comp_time_ms = max_comp_time_ms;
  *out_min_cp_100ms = min_cp_100ms;
  *out_ecm_rep_period_ms = ecm_rep_period_ms;
  return 0;
}

size_t ecmg_build_channel_setup(unsigned char *out, size_t cap, unsigned char version, unsigned super_cas_id) {
  simulcrypt_writer_t w;
  unsigned char cas_id[4];
  psi_put16(cas_id, super_cas_id >> 16);
  psi_put16(cas_id + 2, super_cas_id);
  if (simulcrypt_writer_begin(&w, out, cap, version, ECMG_MSG_CHANNEL_SETUP) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_CHANNEL_ID, (unsigned char[]){0, ECMG_CHANNEL_ID}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_SUPER_CAS_ID, cas_id, sizeof cas_id) < 0)
    return 0;
  return simulcrypt_writer_finish(&w);
}

size_t ecmg_build_stream_setup(unsigned char *out, size_t cap, unsigned char version, unsigned ecm_id, unsigned nominal_cp_100ms) {
  simulcrypt_writer_t w;
  if (simulcrypt_writer_begin(&w, out, cap, version, ECMG_MSG_STREAM_SETUP) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_CHANNEL_ID, (unsigned char[]){0, ECMG_CHANNEL_ID}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_STREAM_ID, (unsigned char[]){0, ECMG_STREAM_ID}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_ID, (unsigned char[]){(unsigned char)(ecm_id >> 8), (unsigned char)ecm_id}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_NOMINAL_CP_DURATION, (unsigned char[]){(unsigned char)(nominal_cp_100ms >> 8), (unsigned char)nominal_cp_100ms}, 2) < 0)
    return 0;
  return simulcrypt_writer_finish(&w);
}

size_t ecmg_build_cw_provision(unsigned char *out, size_t cap, unsigned char version, unsigned short cp_number,
                                  cw_hist_entry_t *hist, size_t cw_len, unsigned lead_cw, unsigned cw_per_msg) {
  simulcrypt_writer_t w;
  unsigned short first_cp = (unsigned short)(cp_number + lead_cw - cw_per_msg + 1);
  unsigned i;
  if (simulcrypt_writer_begin(&w, out, cap, version, ECMG_MSG_CW_PROVISION) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_CHANNEL_ID, (unsigned char[]){0, ECMG_CHANNEL_ID}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_ECM_STREAM_ID, (unsigned char[]){0, ECMG_STREAM_ID}, 2) < 0)
    return 0;
  if (simulcrypt_writer_put_tlv(&w, ECMG_P_CP_NUMBER, (unsigned char[]){(unsigned char)(cp_number >> 8), (unsigned char)cp_number}, 2) < 0)
    return 0;
  for (i = 0; i < cw_per_msg; i++) {
    unsigned char combo[2 + ECMG_MAX_CW_LEN];
    unsigned short cp = (unsigned short)(first_cp + i);
    const unsigned char *cw = hist_get_or_gen(hist, cp, cw_len);
    if (!cw)
      return 0;
    psi_put16(combo, cp);
    memcpy(combo + 2, cw, cw_len);
    if (simulcrypt_writer_put_tlv(&w, ECMG_P_CP_CW_COMBINATION, combo, (unsigned short)(2 + cw_len)) < 0)
      return 0;
  }
  return simulcrypt_writer_finish(&w);
}

/* dials, negotiates protocol_version, opens channel+1 stream.
   0 ok (fills lead_cw/cw_per_msg/max_comp_time_ms, fd left open), -1 err (fd closed) */
static int connect_and_setup(ecmg_client_t *c, int *out_fd, unsigned char *out_version, unsigned *out_lead_cw, unsigned *out_cw_per_msg, unsigned *out_max_comp_time_ms) {
  unsigned char version = c->cfg.version_max;
  int tried_min = (version == c->cfg.version_min);

  for (;;) {
    unsigned char msg[SIMULCRYPT_MAX_FRAME];
    size_t len;
    simulcrypt_reader_t rd;
    simulcrypt_hdr_t hdr;
    const unsigned char *payload;
    int fd = tcp_dial(c, c->cfg.host, c->cfg.port);
    if (fd < 0)
      return -1;

    len = ecmg_build_channel_setup(msg, sizeof msg, version, c->cfg.super_cas_id);
    if (!len || simulcrypt_send_all(fd, msg, len, ECMG_HANDSHAKE_TIMEOUT_MS) < 0) {
      close(fd);
      return -1;
    }
    simulcrypt_reader_init(&rd);
    if (wait_for_message(c, &rd, fd, ECMG_HANDSHAKE_TIMEOUT_MS, &hdr, &payload) != 1) {
      if (!ecmg_stopping(c))
        log_line("ecmg: no channel_status/channel_error reply");
      close(fd);
      return -1;
    }
    if (hdr.type == ECMG_MSG_CHANNEL_ERROR) {
      unsigned short err = 0;
      ecmg_find_error_status(payload, hdr.payload_len, &err);
      close(fd);
      if (err == ECMG_ERR_UNSUPPORTED_PROTOCOL_VERSION && !tried_min) {
        version = c->cfg.version_min;
        tried_min = 1;
        continue;
      }
      log_line("ecmg: channel_setup rejected, error_status=0x%04x", err);
      return -1;
    }
    if (hdr.type != ECMG_MSG_CHANNEL_STATUS) {
      log_line("ecmg: unexpected reply 0x%04x to channel_setup", hdr.type);
      close(fd);
      return -1;
    }

    {
      unsigned lead_cw, cw_per_msg, max_comp_time_ms, min_cp_100ms, ecm_rep_period_ms;
      if (ecmg_parse_channel_status(payload, hdr.payload_len, &lead_cw, &cw_per_msg, &max_comp_time_ms, &min_cp_100ms, &ecm_rep_period_ms) < 0) {
        log_line("ecmg: malformed or unusable channel_status");
        close(fd);
        return -1;
      }
      if (min_cp_100ms && c->cfg.cp_duration_ms < min_cp_100ms * 100)
        log_line("ecmg: --cas-cp-duration %ums is below the ECMG's min_CP_duration %ums, using it anyway", c->cfg.cp_duration_ms, min_cp_100ms * 100);
      *out_lead_cw = lead_cw;
      *out_cw_per_msg = cw_per_msg;
      *out_max_comp_time_ms = max_comp_time_ms ? max_comp_time_ms : 1000;
      atomic_store_explicit(&c->ecm_rep_period_ms, ecm_rep_period_ms, memory_order_relaxed);
    }

    len = ecmg_build_stream_setup(msg, sizeof msg, version, c->cfg.ecm_id, c->cfg.cp_duration_ms / 100);
    if (!len || simulcrypt_send_all(fd, msg, len, ECMG_HANDSHAKE_TIMEOUT_MS) < 0) {
      close(fd);
      return -1;
    }
    if (wait_for_message(c, &rd, fd, ECMG_HANDSHAKE_TIMEOUT_MS, &hdr, &payload) != 1) {
      if (!ecmg_stopping(c))
        log_line("ecmg: no stream_status/stream_error reply");
      close(fd);
      return -1;
    }
    if (hdr.type != ECMG_MSG_STREAM_STATUS) {
      unsigned short err = 0;
      if (hdr.type == ECMG_MSG_STREAM_ERROR)
        ecmg_find_error_status(payload, hdr.payload_len, &err);
      log_line("ecmg: stream_setup rejected, reply=0x%04x error_status=0x%04x", hdr.type, err);
      close(fd);
      return -1;
    }

    *out_fd = fd;
    *out_version = version;
    log_line("ecmg: channel+stream established (protocol_version=0x%02x, lead_cw=%u, cw_per_msg=%u)", version, *out_lead_cw, *out_cw_per_msg);
    return 0;
  }
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

static void *ecmg_client_main(void *arg) {
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

ecmg_client_t *ecmg_client_start(const ecmg_client_cfg_t *cfg, const atomic_ulong *packet_counter, unsigned long packets_per_cp, unsigned long lookahead_margin_packets) {
  ecmg_client_t *c = calloc(1, sizeof *c);
  if (!c)
    return NULL;
  c->cfg = *cfg;
  c->cw_len = scrambler_cw_len(cfg->algo);
  c->packet_counter = packet_counter;
  c->packets_per_cp = packets_per_cp;
  c->lookahead_margin_packets = lookahead_margin_packets;
  pthread_mutex_init(&c->cw_lock, NULL);
  pthread_mutex_init(&c->ecm_lock, NULL);
  if (pthread_create(&c->thread, NULL, ecmg_client_main, c) != 0) {
    log_line("ecmg: pthread_create: %s", strerror(errno));
    pthread_mutex_destroy(&c->cw_lock);
    pthread_mutex_destroy(&c->ecm_lock);
    free(c);
    return NULL;
  }
  return c;
}

void ecmg_client_stop(ecmg_client_t *c) {
  if (!c)
    return;
  atomic_store_explicit(&c->stop, 1, memory_order_relaxed);
  pthread_join(c->thread, NULL);
  pthread_mutex_destroy(&c->cw_lock);
  pthread_mutex_destroy(&c->ecm_lock);
  free(c);
}
