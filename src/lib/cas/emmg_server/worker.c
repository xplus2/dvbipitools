/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/log.h"
#include "lib/signal.h"

#include "../simulcrypt_msg.h"
#include "priv.h"

static void reap_worker_slot(emmg_server_t *s, int slot) {
  if (!s->worker_thread_joinable[slot])
    return;
  pthread_join(s->worker_thread[slot], NULL);
  s->worker_thread_joinable[slot] = 0;
}

static void handle_message(emmg_server_t *s, emmg_conn_state_t *cs, unsigned char version, unsigned short type,
                            const unsigned char *body, size_t body_len, unsigned char *reply, size_t *reply_len, int *should_close) {
  *reply_len = 0;
  *should_close = 0;

  log_line("emmg: rx version=0x%02x type=0x%04x body_len=%zu", version, type, body_len);

  switch (type) {
  case EMMG_MSG_CHANNEL_SETUP: {
    unsigned client_id, data_channel_id;
    if (!simulcrypt_find_u32(body, body_len, EMMG_P_CLIENT_ID, &client_id) || !simulcrypt_find_u16(body, body_len, EMMG_P_DATA_CHANNEL_ID, &data_channel_id)) {
      *should_close = 1;
      return;
    }
    cs->client_id = client_id;
    cs->data_channel_id = data_channel_id;
    cs->have_channel = 1;
    *reply_len = emmg_build_channel_status(reply, SIMULCRYPT_MAX_FRAME, version, cs->client_id, cs->data_channel_id);
    break;
  }
  case EMMG_MSG_CHANNEL_TEST:
    if (!cs->have_channel) {
      *should_close = 1;
      return;
    }
    *reply_len = emmg_build_channel_status(reply, SIMULCRYPT_MAX_FRAME, version, cs->client_id, cs->data_channel_id);
    break;
  case EMMG_MSG_STREAM_SETUP: {
    unsigned data_stream_id, data_id, data_type;
    if (!cs->have_channel) {
      *should_close = 1;
      return;
    }
    if (!simulcrypt_find_u16(body, body_len, EMMG_P_DATA_STREAM_ID, &data_stream_id) || !simulcrypt_find_u16(body, body_len, EMMG_P_DATA_ID, &data_id) || !simulcrypt_find_u8(body, body_len, EMMG_P_DATA_TYPE, &data_type)) {
      *should_close = 1;
      return;
    }
    cs->data_stream_id = data_stream_id;
    cs->data_id = data_id;
    cs->data_type = data_type;
    cs->have_stream = 1;
    *reply_len = emmg_build_stream_status(reply, SIMULCRYPT_MAX_FRAME, version, cs->client_id, cs->data_channel_id, cs->data_stream_id, cs->data_id, cs->data_type);
    break;
  }
  case EMMG_MSG_STREAM_TEST:
    if (!cs->have_stream) {
      *should_close = 1;
      return;
    }
    *reply_len = emmg_build_stream_status(reply, SIMULCRYPT_MAX_FRAME, version, cs->client_id, cs->data_channel_id, cs->data_stream_id, cs->data_id, cs->data_type);
    break;
  case EMMG_MSG_STREAM_BW_REQUEST: {
    unsigned bw = 0;
    int have_bw;
    if (!cs->have_stream) {
      *should_close = 1;
      return;
    }
    have_bw = simulcrypt_find_u16(body, body_len, EMMG_P_BANDWIDTH, &bw);
    *reply_len = emmg_build_stream_bw_allocation(reply, SIMULCRYPT_MAX_FRAME, version, cs->client_id, cs->data_channel_id, cs->data_stream_id, have_bw, bw);
    break;
  }
  case EMMG_MSG_STREAM_CLOSE_REQUEST:
    if (!cs->have_stream) {
      *should_close = 1;
      return;
    }
    *reply_len = emmg_build_stream_close_response(reply, SIMULCRYPT_MAX_FRAME, version, cs->client_id, cs->data_channel_id, cs->data_stream_id);
    *should_close = 1;
    break;
  case EMMG_MSG_CHANNEL_CLOSE:
    *should_close = 1;
    break;
  case EMMG_MSG_CHANNEL_ERROR:
  case EMMG_MSG_STREAM_ERROR: {
    unsigned err = 0;
    simulcrypt_find_u16(body, body_len, EMMG_P_ERROR_STATUS, &err);
    log_line("emmg: client reported error (message 0x%04x, error_status 0x%04x)", type, err);
    *should_close = 1;
    break;
  }
  case EMMG_MSG_DATA_PROVISION: {
    int n;
    if (!cs->have_stream) {
      *should_close = 1;
      return;
    }
    n = emmg_extract_datagrams(body, body_len, publish_datagram_cb, s);
    if (n < 0) {
      *should_close = 1;
      return;
    }
    log_line("emmg: data_provision, %d datagram(s) queued", n);
    break;
  }
  default:
    break; /* clause 4.4.1/5.1.8: unknown message_type is ignored, not an inconsistency */
  }
}

static void *worker_main(void *arg) {
  worker_arg_t *wa = arg;
  emmg_server_t *s = wa->s;
  int fd = wa->fd;
  int slot = wa->slot;
  emmg_conn_state_t cs;
  simulcrypt_reader_t rd;
  int flags;

  memset(&cs, 0, sizeof cs);
  simulcrypt_reader_init(&rd);
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    log_line("emmg: fcntl O_NONBLOCK (slot %d): %s", slot, strerror(errno));
    close(fd);
    free(wa);
    atomic_store_explicit(&s->worker_active[slot], 0, memory_order_release);
    return NULL;
  }
  log_line("emmg: connection accepted (slot %d)", slot);

  while (!atomic_load_explicit(&s->stop, memory_order_relaxed) && !signal_stop_requested()) {
    simulcrypt_hdr_t hdr;
    const unsigned char *payload;
    int rc = simulcrypt_reader_poll(&rd, fd, EMMG_POLL_INTERVAL_MS, &hdr, &payload);
    unsigned char reply[SIMULCRYPT_MAX_FRAME];
    size_t reply_len = 0;
    int should_close = 0;

    if (rc < 0) {
      log_line("emmg: connection closed by peer or read error (slot %d)", slot);
      break;
    }
    if (rc == 0)
      continue;

    handle_message(s, &cs, hdr.version, hdr.type, payload, hdr.payload_len, reply, &reply_len, &should_close);
    if (reply_len) {
      log_line("emmg: tx version=0x%02x type=0x%04x", reply[0], ((unsigned)reply[1] << 8) | reply[2]);
      if (simulcrypt_send_all(fd, reply, reply_len, EMMG_SEND_TIMEOUT_MS) < 0) {
        log_line("emmg: reply send failed (slot %d)", slot);
        break;
      }
    }
    if (should_close)
      break;
  }

  close(fd);
  free(wa);
  atomic_store_explicit(&s->worker_active[slot], 0, memory_order_release);
  return NULL;
}

void *accept_main(void *arg) {
  emmg_server_t *s = arg;

  while (!atomic_load_explicit(&s->stop, memory_order_relaxed) && !signal_stop_requested()) {
    struct pollfd pfd;
    int pret, fd, slot;

    pfd.fd = s->listen_fd;
    pfd.events = POLLIN;
    pret = poll(&pfd, 1, EMMG_POLL_INTERVAL_MS);
    if (pret <= 0)
      continue;

    fd = accept(s->listen_fd, NULL, NULL);
    if (fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        continue;
      log_line("emmg: accept: %s", strerror(errno));
      continue;
    }

    slot = -1;
    for (int i = 0; i < EMMG_MAX_CONNS; i++) {
      int expected = 0;
      if (atomic_compare_exchange_strong_explicit(&s->worker_active[i], &expected, 1, memory_order_acq_rel, memory_order_relaxed)) {
        slot = i;
        break;
      }
    }
    if (slot < 0) {
      log_line("emmg: connection limit (%d) reached, rejecting", EMMG_MAX_CONNS);
      close(fd);
      continue;
    }

    {
      worker_arg_t *wa = malloc(sizeof *wa);
      pthread_t th;
      if (!wa) {
        close(fd);
        atomic_store_explicit(&s->worker_active[slot], 0, memory_order_release);
        continue;
      }
      reap_worker_slot(s, slot);
      wa->s = s;
      wa->fd = fd;
      wa->slot = slot;
      if (pthread_create(&th, NULL, worker_main, wa) != 0) {
        log_line("emmg: pthread_create: %s", strerror(errno));
        close(fd);
        free(wa);
        atomic_store_explicit(&s->worker_active[slot], 0, memory_order_release);
      } else {
        s->worker_thread[slot] = th;
        s->worker_thread_joinable[slot] = 1;
      }
    }
  }
  return NULL;
}
