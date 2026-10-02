/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_TESTS_UNIT_METRICS_SINK_H
#define DVBIPITOOLS_TESTS_UNIT_METRICS_SINK_H

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "lib/metrics/export.h"
#include "lib/sys/ioutil.h"

#define MAX_VALUES 32

typedef struct {
  metrics_id_t id[MAX_VALUES];
  uint64_t value[MAX_VALUES];
  unsigned n;
} seen_t;

typedef struct {
  char dir[64];
  char path[128];
  int rx;
  metrics_exporter_t mx;
} sink_t;

static inline void sink_open(sink_t *s, metrics_component_t component, const char *metrics_id, double interval_s) {
  struct sockaddr_un addr;

  snprintf(s->dir, sizeof s->dir, "/tmp/dvbipitools_metrics_XXXXXX");
  ck_assert_ptr_nonnull(mkdtemp(s->dir));
  snprintf(s->path, sizeof s->path, "%s/m.sock", s->dir);
  s->rx = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  ck_assert_int_ge(s->rx, 0);
  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  bufcpy(addr.sun_path, sizeof addr.sun_path, s->path);
  ck_assert_int_eq(bind(s->rx, (struct sockaddr *)&addr, sizeof addr), 0);
  metrics_exporter_init(&s->mx, component, metrics_id, s->path, interval_s);
}

static inline void sink_close(sink_t *s) {
  metrics_exporter_close(&s->mx);
  close(s->rx);
  unlink(s->path);
  rmdir(s->dir);
}

static inline int sink_read(const sink_t *s, seen_t *seen) {
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  metrics_reader_t r;
  metrics_hdr_t hdr;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t v;
  int got = 0;

  memset(seen, 0, sizeof *seen);
  for (;;) {
    ssize_t n = recv(s->rx, buf, sizeof buf, 0);

    if (n <= 0)
      return got;
    got = 1;
    ck_assert_int_eq(metrics_reader_init(&r, buf, (size_t)n, &hdr), 0);
    while (metrics_reader_next(&r, &id, label, sizeof label, &v) == 1 && seen->n < MAX_VALUES) {
      seen->id[seen->n] = id;
      seen->value[seen->n] = v;
      seen->n++;
    }
  }
}

static inline int seen_has(const seen_t *s, metrics_id_t id, uint64_t *value) {

  for (unsigned i = 0; i < s->n; i++) {
    if (s->id[i] == id) {
      if (value)
        *value = s->value[i];
      return 1;
    }
  }
  return 0;
}

#endif
