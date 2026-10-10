/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_TESTS_UNIT_RUN_HELPER_H
#define DVBIPITOOLS_TESTS_UNIT_RUN_HELPER_H

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"

typedef struct {
  const char *path;
  const char *rewrite;
  unsigned rewrite_ms;
  unsigned stop_ms;
} run_helper_t;

static inline void sleep_ms(unsigned ms) {
  struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};

  nanosleep(&ts, NULL);
}

static inline void *run_helper_thread(void *arg) {
  const run_helper_t *h = arg;

  if (h->rewrite) {
    FILE *f;

    sleep_ms(h->rewrite_ms);
    f = fopen(h->path, "w");
    if (f) {
      fputs(h->rewrite, f);
      fclose(f);
    }
    kill(getpid(), SIGHUP);
    sleep_ms(h->stop_ms - h->rewrite_ms);
  } else {
    sleep_ms(h->stop_ms);
  }
  kill(getpid(), SIGTERM);
  return NULL;
}

static inline unsigned run_helper_free_udp_port(void) {
  struct sockaddr_in a;
  socklen_t len = sizeof a;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  unsigned port;

  ck_assert_int_ge(fd, 0);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&a, &len), 0);
  port = ntohs(a.sin_port);
  close(fd);
  return port;
}

static inline void run_helper_group(char *out, size_t cap, unsigned octet2) {
  unsigned pid = (unsigned)getpid();

  snprintf(out, cap, "239.%u.%u.%u", octet2, pid & 0xFF, (pid >> 8) & 0xFF);
}

#define DVBSTP_SENDER_MAX_DOCS 4
#define DVBSTP_SENDER_PASSES 2
#define DVBSTP_SENDER_GAP_MS 50
#define DVBSTP_SENDER_START_MS 150
#define DVBSTP_SENDER_TAIL_MS 100

typedef struct {
  unsigned payload_id;
  unsigned version;
  int doc;
  unsigned compr;
} send_step_t;

typedef struct {
  const char *group;
  unsigned port;
  const send_step_t *steps;
  unsigned nsteps;
  const unsigned char *docs[DVBSTP_SENDER_MAX_DOCS];
  size_t lens[DVBSTP_SENDER_MAX_DOCS];
} dvbstp_sender_t;

static inline void *dvbstp_sender_thread(void *arg) {
  const dvbstp_sender_t *sd = arg;
  mcast_t *m;

  sleep_ms(DVBSTP_SENDER_START_MS);
  m = mcast_open_send(AF_INET, sd->group, sd->port, NULL, 0);
  if (m) {
    for (unsigned pass = 0; pass < DVBSTP_SENDER_PASSES; pass++)
      for (unsigned i = 0; i < sd->nsteps; i++) {
        const send_step_t *st = &sd->steps[i];
        dvbstp_send_t seg = {.payload_id = st->payload_id, .segment_id = 1, .segment_version = st->version, .compr = st->compr, .want_crc = 1};

        dvbstp_send_segment(m, &seg, sd->docs[st->doc], sd->lens[st->doc]);
        sleep_ms(DVBSTP_SENDER_GAP_MS);
      }
    mcast_close(m);
  }
  sleep_ms(DVBSTP_SENDER_TAIL_MS);
  kill(getpid(), SIGTERM);
  return NULL;
}

static inline unsigned run_helper_port(unsigned idx) {
  return (20000u + ((unsigned)getpid() * 257u + idx * 2u) % 10000u) & ~1u;
}

static inline const char *run_helper_group_n(unsigned idx) {
  static char bufs[256][32];
  unsigned pid = (unsigned)getpid();

  idx &= 0xFFu;
  snprintf(bufs[idx], sizeof bufs[idx], "239.%u.%u.%u", 100u + idx % 100u, pid & 0xFF, (pid >> 8) & 0xFF);
  return bufs[idx];
}

#endif
