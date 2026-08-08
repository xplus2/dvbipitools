/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/log.h"
#include "lib/mux/psi_build.h"
#include "lib/signal.h"

#include "emmg_server.h"
#include "simulcrypt_msg.h"

#define EMMG_MAX_CONNS 8
#define EMMG_QUEUE_CAP 256
#define EMMG_POLL_INTERVAL_MS 150
#define EMMG_SEND_TIMEOUT_MS 3000

typedef struct {
  unsigned char data[EMMG_MAX_DATAGRAM_LEN];
  size_t len;
} emmg_queued_datagram_t;

struct emmg_server {
  int listen_fd;
  atomic_int stop;
  pthread_t accept_thread;

  atomic_int worker_active[EMMG_MAX_CONNS]; /* detached threads; stop waits on this, doesn't join */

  pthread_mutex_t queue_lock;
  emmg_queued_datagram_t queue[EMMG_QUEUE_CAP];
  size_t queue_head;
  atomic_size_t queue_len; /* mutex-protected writes, lock-free read for dequeue's empty pre-check */

  atomic_ulong emm_total;
};

typedef struct {
  int have_channel;
  unsigned client_id;
  unsigned data_channel_id;
  int have_stream;
  unsigned data_stream_id;
  unsigned data_id;
  unsigned data_type;
} emmg_conn_state_t;

typedef struct {
  emmg_server_t *s;
  int fd;
  int slot;
} worker_arg_t;

static int find_u16(const unsigned char *body, size_t body_len, unsigned short tag, unsigned *out) {
  simulcrypt_tlv_reader_t it;
  unsigned short t, vlen;
  const unsigned char *val;
  simulcrypt_tlv_reader_init(&it, body, body_len);
  while (simulcrypt_tlv_reader_next(&it, &t, &val, &vlen) == 1) {
    if (t == tag && vlen == 2) {
      *out = ((unsigned)val[0] << 8) | val[1];
      return 1;
    }
  }
  return 0;
}

static int find_u32(const unsigned char *body, size_t body_len, unsigned short tag, unsigned *out) {
  simulcrypt_tlv_reader_t it;
  unsigned short t, vlen;
  const unsigned char *val;
  simulcrypt_tlv_reader_init(&it, body, body_len);
  while (simulcrypt_tlv_reader_next(&it, &t, &val, &vlen) == 1) {
    if (t == tag && vlen == 4) {
      *out = ((unsigned)val[0] << 24) | ((unsigned)val[1] << 16) | ((unsigned)val[2] << 8) | val[3];
      return 1;
    }
  }
  return 0;
}

static int find_u8(const unsigned char *body, size_t body_len, unsigned short tag, unsigned *out) {
  simulcrypt_tlv_reader_t it;
  unsigned short t, vlen;
  const unsigned char *val;
  simulcrypt_tlv_reader_init(&it, body, body_len);
  while (simulcrypt_tlv_reader_next(&it, &t, &val, &vlen) == 1) {
    if (t == tag && vlen == 1) {
      *out = val[0];
      return 1;
    }
  }
  return 0;
}

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

static void publish_datagram_cb(const unsigned char *data, unsigned short len, void *user) {
  emmg_server_t *s = user;
  if (len > EMMG_MAX_DATAGRAM_LEN) {
    log_line("emmg: dropping oversized EMM datagram (%u bytes)", (unsigned)len);
    return;
  }
  pthread_mutex_lock(&s->queue_lock);
  if (atomic_load_explicit(&s->queue_len, memory_order_relaxed) == EMMG_QUEUE_CAP) {
    s->queue_head = (s->queue_head + 1) % EMMG_QUEUE_CAP;
    atomic_fetch_sub_explicit(&s->queue_len, 1, memory_order_relaxed);
    log_line("emmg: EMM queue full, dropping oldest datagram");
  }
  {
    size_t idx = (s->queue_head + atomic_load_explicit(&s->queue_len, memory_order_relaxed)) % EMMG_QUEUE_CAP;
    memcpy(s->queue[idx].data, data, len);
    s->queue[idx].len = len;
    atomic_fetch_add_explicit(&s->queue_len, 1, memory_order_relaxed);
  }
  pthread_mutex_unlock(&s->queue_lock);
  atomic_fetch_add_explicit(&s->emm_total, 1, memory_order_relaxed);
}

int emmg_server_dequeue_emm(emmg_server_t *s, unsigned char *out, size_t cap, size_t *len_out) {
  int have;

  /* called every packet, almost always empty: skip lock on miss. */
  if (!atomic_load_explicit(&s->queue_len, memory_order_relaxed))
    return -1;

  pthread_mutex_lock(&s->queue_lock);
  have = atomic_load_explicit(&s->queue_len, memory_order_relaxed) > 0;
  if (have) {
    size_t len = s->queue[s->queue_head].len;
    if (cap < len) {
      pthread_mutex_unlock(&s->queue_lock);
      return -1;
    }
    memcpy(out, s->queue[s->queue_head].data, len);
    *len_out = len;
    s->queue_head = (s->queue_head + 1) % EMMG_QUEUE_CAP;
    atomic_fetch_sub_explicit(&s->queue_len, 1, memory_order_relaxed);
  }
  pthread_mutex_unlock(&s->queue_lock);
  return have ? 0 : -1;
}

unsigned emmg_server_client_count(emmg_server_t *s) {
  int i, n = 0;
  for (i = 0; i < EMMG_MAX_CONNS; i++)
    if (atomic_load_explicit(&s->worker_active[i], memory_order_relaxed))
      n++;
  return (unsigned)n;
}

unsigned long emmg_server_emm_total(emmg_server_t *s) { return atomic_load_explicit(&s->emm_total, memory_order_relaxed); }

static void handle_message(emmg_server_t *s, emmg_conn_state_t *cs, unsigned char version, unsigned short type,
                            const unsigned char *body, size_t body_len, unsigned char *reply, size_t *reply_len, int *should_close) {
  *reply_len = 0;
  *should_close = 0;

  log_line("emmg: rx version=0x%02x type=0x%04x body_len=%zu", version, type, body_len);

  switch (type) {
  case EMMG_MSG_CHANNEL_SETUP: {
    unsigned client_id, data_channel_id;
    if (!find_u32(body, body_len, EMMG_P_CLIENT_ID, &client_id) || !find_u16(body, body_len, EMMG_P_DATA_CHANNEL_ID, &data_channel_id)) {
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
    if (!find_u16(body, body_len, EMMG_P_DATA_STREAM_ID, &data_stream_id) || !find_u16(body, body_len, EMMG_P_DATA_ID, &data_id) || !find_u8(body, body_len, EMMG_P_DATA_TYPE, &data_type)) {
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
    have_bw = find_u16(body, body_len, EMMG_P_BANDWIDTH, &bw);
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
    find_u16(body, body_len, EMMG_P_ERROR_STATUS, &err);
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

  pthread_detach(pthread_self());
  memset(&cs, 0, sizeof cs);
  simulcrypt_reader_init(&rd);
  flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
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

static int tcp_listen_dualstack(unsigned port) {
  struct sockaddr_in6 addr;
  int fd, on = 1, off = 0, flags;

  fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0) {
    log_line("emmg: socket: %s", strerror(errno));
    return -1;
  }
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);

  memset(&addr, 0, sizeof addr);
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = in6addr_any;
  addr.sin6_port = htons((unsigned short)port);
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
    log_line("emmg: bind :%u: %s", port, strerror(errno));
    close(fd);
    return -1;
  }
  if (listen(fd, 8) < 0) {
    log_line("emmg: listen: %s", strerror(errno));
    close(fd);
    return -1;
  }
  flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  return fd;
}

static void *accept_main(void *arg) {
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
      wa->s = s;
      wa->fd = fd;
      wa->slot = slot;
      if (pthread_create(&th, NULL, worker_main, wa) != 0) {
        log_line("emmg: pthread_create: %s", strerror(errno));
        close(fd);
        free(wa);
        atomic_store_explicit(&s->worker_active[slot], 0, memory_order_release);
      }
    }
  }
  return NULL;
}

emmg_server_t *emmg_server_start(const emmg_server_cfg_t *cfg) {
  emmg_server_t *s = calloc(1, sizeof *s);
  if (!s)
    return NULL;

  s->listen_fd = tcp_listen_dualstack(cfg->port);
  if (s->listen_fd < 0) {
    free(s);
    return NULL;
  }
  pthread_mutex_init(&s->queue_lock, NULL);

  if (pthread_create(&s->accept_thread, NULL, accept_main, s) != 0) {
    log_line("emmg: pthread_create: %s", strerror(errno));
    close(s->listen_fd);
    pthread_mutex_destroy(&s->queue_lock);
    free(s);
    return NULL;
  }
  return s;
}

/* useful when cfg.port was 0 (kernel-assigned ephemeral port) */
unsigned emmg_server_port(emmg_server_t *s) {
  struct sockaddr_in6 addr;
  socklen_t alen = sizeof addr;
  if (getsockname(s->listen_fd, (struct sockaddr *)&addr, &alen) < 0)
    return 0;
  return ntohs(addr.sin6_port);
}

void emmg_server_stop(emmg_server_t *s) {
  int waited_ms = 0;
  if (!s)
    return;
  atomic_store_explicit(&s->stop, 1, memory_order_relaxed);
  pthread_join(s->accept_thread, NULL);
  close(s->listen_fd);

  /* workers are detached (slots can be reused across a connection's lifetime, unsafe to join by stored handle)
     wait for worker_active to clear. workers check stop flag < every EMMG_POLL_INTERVAL_MS */
  for (;;) {
    int i, any_active = 0;
    struct timespec ts;
    for (i = 0; i < EMMG_MAX_CONNS; i++)
      if (atomic_load_explicit(&s->worker_active[i], memory_order_acquire))
        any_active = 1;
    if (!any_active || waited_ms >= 2000)
      break;
    ts.tv_sec = 0;
    ts.tv_nsec = 50 * 1000000L;
    nanosleep(&ts, NULL);
    waited_ms += 50;
  }
  pthread_mutex_destroy(&s->queue_lock);
  free(s);
}
