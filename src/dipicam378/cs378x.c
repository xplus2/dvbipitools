/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/log.h"
#include "lib/signal.h"

#include "cs378x.h"
#include "version.h"

#define CS378X_MAX_CONNS 4
#define CS378X_POLL_INTERVAL_MS 150
#define CS378X_RECV_TIMEOUT_MS 200
#define CS378X_BUF_CAP 2048
#define CS378X_MIN_FRAME 36 /* 4-byte ucrc + 2 AES blocks */

/* command bytes, camd35/cs378x family */
#define CMD_ECM_REQUEST 0
#define CMD_ECM_RESPONSE 1
#define CMD_EMM 6
#define CMD_EMM_1830 19
#define CMD_KEEPALIVE 55
#define CMD_INVALID 8 /* "stop sending requests for srvid/prvid/caid" */

static uint32_t crc32_table[256];

void cs378x_crc32_init_table(void) {
  uint32_t c;
  int n, k;
  for (n = 0; n < 256; n++) {
    c = (uint32_t)n;
    for (k = 0; k < 8; k++)
      c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    crc32_table[n] = c;
  }
}

/* zlib-standard CRC-32 (reflected, poly 0xEDB88320) */
uint32_t cs378x_crc32(const unsigned char *buf, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  size_t i;
  for (i = 0; i < len; i++)
    crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

int cs378x_md5(const unsigned char *data, size_t len, unsigned char out[16]) {
  unsigned int outlen = 0;
  return (EVP_Digest(data, len, out, &outlen, EVP_md5(), NULL) == 1 && outlen == 16) ? 0 : -1;
}

/* AES-128, plain ECB, no padding */
int cs378x_aes128_ecb(const unsigned char key[16], unsigned char *buf, size_t len, int encrypt) {
  EVP_CIPHER_CTX *ctx;
  int outlen, ok = 0;
  if (len == 0 || len % 16 != 0)
    return -1;
  ctx = EVP_CIPHER_CTX_new();
  if (!ctx)
    return -1;
  if (encrypt ? EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL) != 1
              : EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL) != 1)
    goto done;
  EVP_CIPHER_CTX_set_padding(ctx, 0);
  if (encrypt ? EVP_EncryptUpdate(ctx, buf, &outlen, buf, (int)len) != 1 : EVP_DecryptUpdate(ctx, buf, &outlen, buf, (int)len) != 1)
    goto done;
  ok = ((size_t)outlen == len);
done:
  EVP_CIPHER_CTX_free(ctx);
  return ok ? 0 : -1;
}

/* rounds n up to the next multiple of 16 */
size_t cs378x_frame_boundary(size_t n) {
  return (((n - 1) >> 4) + 1) << 4;
}

struct cs378x_server {
  int listen_fd;
  atomic_int stop;
  pthread_t accept_thread;
  atomic_int worker_active[CS378X_MAX_CONNS];

  unsigned char aes_key[16]; /* MD5(password) */
  unsigned char expected_ucrc[4]; /* crc32(MD5(username)); only checked if check_ucrc */
  int check_ucrc;
  int verbose;

  cs378x_ecm_cb ecm_cb;
  cs378x_emm_cb emm_cb;
  void *user;
};

typedef struct {
  cs378x_server_t *s;
  int fd;
  int slot;
} worker_arg_t;

/* read n bytes, honoring stop flag between recv() timeouts. 1 ok, 0 stopped, -1 closed/error */
static int read_exact(int fd, unsigned char *buf, size_t n, atomic_int *stop) {
  size_t got = 0;
  while (got < n) {
    ssize_t r;
    if (atomic_load_explicit(stop, memory_order_relaxed) || signal_stop_requested())
      return 0;
    r = recv(fd, buf + got, n - got, 0);
    if (r > 0) {
      got += (size_t)r;
      continue;
    }
    if (r == 0)
      return -1;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      continue;
    return -1;
  }
  return 1;
}

static ssize_t send_all(int fd, const unsigned char *buf, size_t n) {
  size_t sent = 0;
  while (sent < n) {
    ssize_t w = send(fd, buf + sent, n - sent, 0);
    if (w <= 0) {
      if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        continue;
      return -1;
    }
    sent += (size_t)w;
  }
  return (ssize_t)sent;
}

/* body decrypted in place; mutated into the CMD01 answer. conn_ucrc echoed verbatim */
static void send_ecm_response(cs378x_server_t *s, int fd, const unsigned char conn_ucrc[4], unsigned char *body, const unsigned char cw[16]) {
  size_t total;
  uint32_t crc;

  body[0] = CMD_ECM_RESPONSE;
  body[1] = 16;
  memcpy(body + 20, cw, 16);
  total = cs378x_frame_boundary(20 + 16);
  memset(body + 36, 0xFF, total - 36);
  crc = cs378x_crc32(body + 20, 16);
  body[4] = (unsigned char)(crc >> 24);
  body[5] = (unsigned char)(crc >> 16);
  body[6] = (unsigned char)(crc >> 8);
  body[7] = (unsigned char)crc;

  if (cs378x_aes128_ecb(s->aes_key, body, total, 1) != 0) {
    log_line(TOOL_NAME ": encrypt failed building ECM response");
    return;
  }
  {
    unsigned char frame[4 + CS378X_BUF_CAP];
    memcpy(frame, conn_ucrc, 4);
    memcpy(frame + 4, body, total);
    if (send_all(fd, frame, 4 + total) < 0)
      log_line(TOOL_NAME ": send ECM response failed: %s", strerror(errno));
  }
}

/* body decrypted in place. E_INVALID only, no sleep variant - stop-asking for this
   srvid/prid/caid, still at body[8:20), out like send_ecm_response */
static void send_cmd08(cs378x_server_t *s, int fd, const unsigned char conn_ucrc[4], unsigned char *body) {
  size_t total;
  uint32_t crc;

  body[0] = CMD_INVALID;
  body[1] = 2;
  body[20] = 0;
  body[21] = 0;
  total = cs378x_frame_boundary(20 + 2);
  memset(body + 22, 0xFF, total - 22);
  crc = cs378x_crc32(body + 20, 2);
  body[4] = (unsigned char)(crc >> 24);
  body[5] = (unsigned char)(crc >> 16);
  body[6] = (unsigned char)(crc >> 8);
  body[7] = (unsigned char)crc;

  if (cs378x_aes128_ecb(s->aes_key, body, total, 1) != 0) {
    log_line(TOOL_NAME ": encrypt failed building CMD08");
    return;
  }
  {
    unsigned char frame[4 + CS378X_BUF_CAP];
    memcpy(frame, conn_ucrc, 4);
    memcpy(frame + 4, body, total);
    if (send_all(fd, frame, 4 + total) < 0)
      log_line(TOOL_NAME ": send CMD08 failed: %s", strerror(errno));
  }
}

static void send_keepalive_answer(cs378x_server_t *s, int fd, const unsigned char conn_ucrc[4]) {
  unsigned char body[32];
  uint32_t crc;
  memset(body, 0, sizeof body);
  body[0] = CMD_KEEPALIVE;
  body[1] = 1;
  body[2] = 0;
  crc = cs378x_crc32(body + 20, 1);
  body[4] = (unsigned char)(crc >> 24);
  body[5] = (unsigned char)(crc >> 16);
  body[6] = (unsigned char)(crc >> 8);
  body[7] = (unsigned char)crc;
  if (cs378x_aes128_ecb(s->aes_key, body, sizeof body, 1) != 0)
    return;
  {
    unsigned char frame[4 + 32];
    memcpy(frame, conn_ucrc, 4);
    memcpy(frame + 4, body, sizeof body);
    send_all(fd, frame, sizeof frame);
  }
}

static void handle_ecm(cs378x_server_t *s, int fd, const unsigned char conn_ucrc[4], unsigned char *body, size_t buflen) {
  unsigned srvid, caid, prid;
  unsigned char cw[16];
  int rc;

  srvid = ((unsigned)body[8] << 8) | body[9];
  caid = ((unsigned)body[10] << 8) | body[11];
  prid = ((unsigned)body[12] << 24) | ((unsigned)body[13] << 16) | ((unsigned)body[14] << 8) | body[15];

  if (s->verbose)
    log_line(TOOL_NAME ": ECM request srvid=%04X caid=%04X prid=%08X len=%zu", srvid, caid, prid, buflen);

  rc = s->ecm_cb ? s->ecm_cb(body + 20, buflen, srvid, caid, prid, cw, s->user) : -1;
  if (rc == 0) {
    send_ecm_response(s, fd, conn_ucrc, body, cw);
    return;
  }
  if (rc == -2) {
    log_line(TOOL_NAME ": caid %04X not supported, sending CMD08 for srvid=%04X", caid, srvid);
    send_cmd08(s, fd, conn_ucrc, body);
    return;
  }
  if (s->verbose)
    log_line(TOOL_NAME ": no CW available yet, dropping request");
}

static void handle_emm(cs378x_server_t *s, unsigned char *body, size_t buflen) {
  unsigned caid, provid;

  caid = ((unsigned)body[10] << 8) | body[11];
  provid = ((unsigned)body[12] << 24) | ((unsigned)body[13] << 16) | ((unsigned)body[14] << 8) | body[15];

  if (s->verbose)
    log_line(TOOL_NAME ": EMM caid=%04X provid=%08X len=%zu", caid, provid, buflen);

  if (s->emm_cb)
    s->emm_cb(body + 20, buflen, caid, provid, s->user);
}

static void *worker_main(void *arg) {
  worker_arg_t *wa = arg;
  cs378x_server_t *s = wa->s;
  int fd = wa->fd;
  int slot = wa->slot;
  struct timeval tv;
  unsigned char conn_ucrc[4];
  int have_ucrc = 0;

  pthread_detach(pthread_self());
  tv.tv_sec = 0;
  tv.tv_usec = CS378X_RECV_TIMEOUT_MS * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  log_line(TOOL_NAME ": connection accepted (slot %d)", slot);

  for (;;) {
    unsigned char buf[CS378X_BUF_CAP];
    unsigned char *body = buf + 4;
    size_t buflen, total;
    int rc;
    rc = read_exact(fd, buf, CS378X_MIN_FRAME, &s->stop);
    if (rc <= 0)
      break;

    if (!have_ucrc) {
      if (s->check_ucrc && memcmp(buf, s->expected_ucrc, 4) != 0) {
        log_line(TOOL_NAME ": username mismatch, closing (slot %d)", slot);
        break;
      }
      memcpy(conn_ucrc, buf, 4);
      have_ucrc = 1;
    } else if (memcmp(buf, conn_ucrc, 4) != 0) {
      log_line(TOOL_NAME ": connection id changed mid-stream, closing (slot %d)", slot);
      break;
    }

    if (cs378x_aes128_ecb(s->aes_key, body, 32, 0) != 0)
      break;

    if (body[0] == CMD_ECM_REQUEST)
      buflen = (size_t)(3 + ((body[21] & 0x0F) << 8) + body[22]);
    else
      buflen = body[1];

    total = cs378x_frame_boundary(20 + buflen);
    if (total > CS378X_BUF_CAP - 4) {
      log_line(TOOL_NAME ": oversized request (%zu), closing (slot %d)", total, slot);
      break;
    }

    if (total > 32) {
      rc = read_exact(fd, body + 32, total - 32, &s->stop);
      if (rc <= 0)
        break;
      if (cs378x_aes128_ecb(s->aes_key, body + 32, total - 32, 0) != 0)
        break;
    }

    if (cs378x_crc32(body + 20, buflen) != (((uint32_t)body[4] << 24) | ((uint32_t)body[5] << 16) | ((uint32_t)body[6] << 8) | body[7])) {
      log_line(TOOL_NAME ": checksum error, wrong password? (slot %d)", slot);
      break;
    }

    switch (body[0]) {
      case CMD_ECM_REQUEST:
        handle_ecm(s, fd, conn_ucrc, body, buflen);
        break;
      case CMD_EMM:
      case CMD_EMM_1830:
        handle_emm(s, body, buflen);
        break;
      case CMD_KEEPALIVE:
        send_keepalive_answer(s, fd, conn_ucrc);
        break;
      default:
        if (s->verbose) {
          static const char hex_nib[] = "0123456789ABCDEF";
          size_t dumplen = buflen + 20 > 32 ? 32 : buflen + 20;
          char hex[32 * 3 + 1];
          size_t i;
          for (i = 0; i < dumplen; i++) {
            hex[i * 3] = hex_nib[(body[i] >> 4) & 0xF];
            hex[i * 3 + 1] = hex_nib[body[i] & 0xF];
            hex[i * 3 + 2] = ' ';
          }
          hex[dumplen * 3] = '\0';
          log_line(TOOL_NAME ": unknown command %d, len=%zu, bytes=%s(slot %d)", body[0], buflen, hex, slot);
        }
        break;
    }
  }

  log_line(TOOL_NAME ": connection closed (slot %d)", slot);
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
    log_line(TOOL_NAME ": socket: %s", strerror(errno));
    return -1;
  }
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);

  memset(&addr, 0, sizeof addr);
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = in6addr_any;
  addr.sin6_port = htons((unsigned short)port);
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
    log_line(TOOL_NAME ": bind :%u: %s", port, strerror(errno));
    close(fd);
    return -1;
  }
  if (listen(fd, 8) < 0) {
    log_line(TOOL_NAME ": listen: %s", strerror(errno));
    close(fd);
    return -1;
  }
  flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  return fd;
}

static void *accept_main(void *arg) {
  cs378x_server_t *s = arg;

  while (!atomic_load_explicit(&s->stop, memory_order_relaxed) && !signal_stop_requested()) {
    struct pollfd pfd;
    int pret, fd, slot, i;

    pfd.fd = s->listen_fd;
    pfd.events = POLLIN;
    pret = poll(&pfd, 1, CS378X_POLL_INTERVAL_MS);
    if (pret <= 0)
      continue;

    fd = accept(s->listen_fd, NULL, NULL);
    if (fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        continue;
      log_line(TOOL_NAME ": accept: %s", strerror(errno));
      continue;
    }

    slot = -1;
    for (i = 0; i < CS378X_MAX_CONNS; i++) {
      int expected = 0;
      if (atomic_compare_exchange_strong_explicit(&s->worker_active[i], &expected, 1, memory_order_acq_rel, memory_order_relaxed)) {
        slot = i;
        break;
      }
    }
    if (slot < 0) {
      log_line(TOOL_NAME ": connection limit (%d) reached, rejecting", CS378X_MAX_CONNS);
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
        log_line(TOOL_NAME ": pthread_create: %s", strerror(errno));
        close(fd);
        free(wa);
        atomic_store_explicit(&s->worker_active[slot], 0, memory_order_release);
      }
    }
  }
  return NULL;
}

cs378x_server_t *cs378x_server_start(const cs378x_cfg_t *cfg, cs378x_ecm_cb ecm_cb, cs378x_emm_cb emm_cb, void *user) {
  cs378x_server_t *s;

  cs378x_crc32_init_table();

  s = calloc(1, sizeof *s);
  if (!s)
    return NULL;

  if (cs378x_md5((const unsigned char *)cfg->password, strlen(cfg->password), s->aes_key) != 0) {
    free(s);
    return NULL;
  }
  if (cfg->username && cfg->username[0]) {
    unsigned char md5tmp[16];
    uint32_t crc;
    if (cs378x_md5((const unsigned char *)cfg->username, strlen(cfg->username), md5tmp) != 0) {
      free(s);
      return NULL;
    }
    crc = cs378x_crc32(md5tmp, sizeof md5tmp);
    s->expected_ucrc[0] = (unsigned char)(crc >> 24);
    s->expected_ucrc[1] = (unsigned char)(crc >> 16);
    s->expected_ucrc[2] = (unsigned char)(crc >> 8);
    s->expected_ucrc[3] = (unsigned char)crc;
    s->check_ucrc = 1;
  }
  s->verbose = cfg->verbose;
  s->ecm_cb = ecm_cb;
  s->emm_cb = emm_cb;
  s->user = user;

  s->listen_fd = tcp_listen_dualstack(cfg->port);
  if (s->listen_fd < 0) {
    free(s);
    return NULL;
  }

  if (pthread_create(&s->accept_thread, NULL, accept_main, s) != 0) {
    log_line(TOOL_NAME ": pthread_create: %s", strerror(errno));
    close(s->listen_fd);
    free(s);
    return NULL;
  }
  return s;
}

void cs378x_server_stop(cs378x_server_t *s) {
  int waited_ms = 0;
  if (!s)
    return;
  atomic_store_explicit(&s->stop, 1, memory_order_relaxed);
  pthread_join(s->accept_thread, NULL);
  close(s->listen_fd);

  for (;;) {
    int i, any_active = 0;
    for (i = 0; i < CS378X_MAX_CONNS; i++)
      if (atomic_load_explicit(&s->worker_active[i], memory_order_acquire))
        any_active = 1;
    if (!any_active || waited_ms > 5000)
      break;
    usleep(50 * 1000);
    waited_ms += 50;
  }
  free(s);
}
