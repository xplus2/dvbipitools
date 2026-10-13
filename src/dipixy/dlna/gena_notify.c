/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/helper/log.h"
#include "lib/net/netconnect.h"
#include "lib/sys/ioutil.h"
#include "lib/sys/signal.h"

#include "../version.h"
#include "dlna_int.h"

#define QUEUE_MAX 64
#define IO_TIMEOUT_MS 2000

typedef struct {
  char host[48];
  unsigned port;
  char path[128];
  char sid[64];
  unsigned seq;
  char body[1024];
  unsigned delay_ms;
} job_t;

static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static job_t g_queue[QUEUE_MAX];
static size_t g_head, g_count;
static int g_started, g_stop;
static pthread_t g_thread;

static int send_all(int fd, const char *p, size_t n) {
  while (n) {
    ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
    if (w > 0) {
      p += w;
      n -= (size_t)w;
      continue;
    }
    if (w < 0 && errno == EINTR) continue;
    if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      struct pollfd pfd = {fd, POLLOUT, 0};
      if (poll(&pfd, 1, IO_TIMEOUT_MS) <= 0) return -1;
      continue;
    }
    return -1;
  }
  return 0;
}

/* 0 on 2xx reply */
static int read_status(int fd) {
  struct pollfd pfd = {fd, POLLIN, 0};
  char buf[16];
  ssize_t n;
  if (poll(&pfd, 1, IO_TIMEOUT_MS) <= 0) return -1;
  n = recv(fd, buf, sizeof buf - 1, 0);
  if (n < 12 || memcmp(buf, "HTTP/1.", 7) || buf[9] != '2') return -1;
  return 0;
}

static int deliver(const job_t *j) {
  char hdr[512];
  char len[16];
  sbuf_t b;
  size_t body_len = strlen(j->body);
  int v6 = strchr(j->host, ':') != NULL;
  int fd = netconnect_tcp(j->host, j->port, IO_TIMEOUT_MS, NULL);
  int rc;
  if (fd < 0) return -1;
  uint_to_str(len, (unsigned)body_len);
  sbuf_init(&b, hdr, sizeof hdr);
  sbuf_add(&b, "NOTIFY ");
  sbuf_add(&b, j->path);
  sbuf_add(&b, " HTTP/1.1\r\nHOST: ");
  sbuf_add(&b, v6 ? "[" : "");
  sbuf_add(&b, j->host);
  sbuf_add(&b, v6 ? "]:" : ":");
  sbuf_add_uint(&b, j->port);
  sbuf_add(&b, "\r\nCONTENT-TYPE: text/xml; charset=\"utf-8\"\r\nNT: upnp:event\r\nNTS: upnp:propchange\r\nSID: ");
  sbuf_add(&b, j->sid);
  sbuf_add(&b, "\r\nSEQ: ");
  sbuf_add_uint(&b, j->seq);
  sbuf_add(&b, "\r\nCONTENT-LENGTH: ");
  sbuf_add(&b, len);
  sbuf_add(&b, "\r\nCONNECTION: close\r\n\r\n");
  rc = b.truncated ? -1 : send_all(fd, hdr, b.len);
  if (!rc) rc = send_all(fd, j->body, body_len);
  if (!rc) rc = read_status(fd);
  close(fd);
  return rc;
}

static void *worker(void *arg) {
  job_t j;
  (void)arg;
  pthread_mutex_lock(&g_mtx);
  while (!g_stop) {
    if (!g_count) {
      pthread_cond_wait(&g_cv, &g_mtx);
      continue;
    }
    j = g_queue[g_head];
    g_head = (g_head + 1) % QUEUE_MAX;
    g_count--;
    pthread_mutex_unlock(&g_mtx);
    if (j.delay_ms) usleep(j.delay_ms * 1000u);
    if (deliver(&j)) {
      log_line(TOOL_NAME ": gena: NOTIFY to %s:%u failed, dropping subscription", j.host, j.port);
      gena_delivery_failed(j.sid);
    }
    pthread_mutex_lock(&g_mtx);
  }
  pthread_mutex_unlock(&g_mtx);
  return NULL;
}

int gena_notify_enqueue(const char *host, unsigned port, const char *path, const char *sid, unsigned seq, const char *body, unsigned delay_ms) {
  job_t *j;
  int rc = 0;
  pthread_mutex_lock(&g_mtx);
  if (!g_started && !g_stop) {
    if (pthread_create(&g_thread, NULL, worker, NULL)) rc = -1;
    else g_started = 1;
  }
  if (!rc && (!g_started || g_count == QUEUE_MAX || strlen(body) >= sizeof j->body)) rc = -1;
  if (!rc) {
    j = &g_queue[(g_head + g_count) % QUEUE_MAX];
    bufcpy(j->host, sizeof j->host, host);
    j->port = port;
    bufcpy(j->path, sizeof j->path, path);
    bufcpy(j->sid, sizeof j->sid, sid);
    j->seq = seq;
    bufcpy(j->body, sizeof j->body, body);
    j->delay_ms = delay_ms;
    g_count++;
    pthread_cond_signal(&g_cv);
  }
  pthread_mutex_unlock(&g_mtx);
  return rc;
}

void gena_notify_stop(void) {
  int join;
  pthread_mutex_lock(&g_mtx);
  join = g_started;
  g_started = 0;
  g_stop = 1;
  g_count = 0;
  pthread_cond_broadcast(&g_cv);
  pthread_mutex_unlock(&g_mtx);
  if (join) pthread_join(g_thread, NULL);
  pthread_mutex_lock(&g_mtx);
  g_stop = 0;
  pthread_mutex_unlock(&g_mtx);
}
