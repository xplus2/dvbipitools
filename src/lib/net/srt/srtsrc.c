/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/signal.h"

#include "srtin.h"
#include "srtsrc.h"

struct srtsrc {
  int pfd[2];
  pthread_t thread;
  atomic_int stop;
  char host[64];
  char passphrase[128];
  char streamid[128];
  char packetfilter[256];
  srtin_cfg_t cfg; /* peers[0]/opts point into the buffers above */
};

static void *reader_main(void *arg) {
  srtsrc_t *r = arg;
  srtin_t *s = srtin_open(&r->cfg);

  if (!s) {
    close(r->pfd[1]);
    return NULL;
  }
  while (!atomic_load_explicit(&r->stop, memory_order_relaxed) && !signal_stop_requested()) {
    unsigned char buf[65536];
    int reconnected;
    int n = srtin_read(s, buf, sizeof buf, &reconnected);

    if (n < 0)
      break;
    if (n == 0)
      continue;
    if (pipe_write_all(r->pfd[1], buf, (size_t)n, &r->stop) < 0)
      break;
  }
  srtin_close(s);
  close(r->pfd[1]); /* next read() on pfd[0] sees EOF, stop or error alike */
  return NULL;
}

srtsrc_t *srtsrc_open(const srtsrc_cfg_t *cfg) {
  srtsrc_t *r;

  if (!cfg->host || !cfg->port)
    return NULL;

  r = calloc(1, sizeof *r);
  if (!r)
    return NULL;

  if (pipe(r->pfd) < 0) {
    free(r);
    return NULL;
  }
  if (fcntl(r->pfd[1], F_SETFL, O_NONBLOCK) < 0) {
    close(r->pfd[0]);
    close(r->pfd[1]);
    free(r);
    return NULL;
  }

  bufcpy(r->host, sizeof r->host, cfg->host);
  if (cfg->passphrase)
    bufcpy(r->passphrase, sizeof r->passphrase, cfg->passphrase);
  if (cfg->streamid)
    bufcpy(r->streamid, sizeof r->streamid, cfg->streamid);
  if (cfg->packetfilter)
    bufcpy(r->packetfilter, sizeof r->packetfilter, cfg->packetfilter);

  r->cfg.peers[0].host = r->host;
  r->cfg.peers[0].port = cfg->port;
  r->cfg.npeers = 1;
  r->cfg.group_mode = SRTGROUP_NONE;
  r->cfg.listen = cfg->listen;
  r->cfg.opts.passphrase = r->passphrase[0] ? r->passphrase : NULL;
  r->cfg.opts.pbkeylen = cfg->pbkeylen;
  r->cfg.opts.streamid = r->streamid[0] ? r->streamid : NULL;
  r->cfg.opts.packetfilter = r->packetfilter[0] ? r->packetfilter : NULL;
  r->cfg.opts.latency_ms = cfg->latency_ms;
  r->cfg.verbose = cfg->verbose;
  r->cfg.mx = cfg->mx;
  r->cfg.tool_version = cfg->tool_version;

  atomic_init(&r->stop, 0);
  if (pthread_create(&r->thread, NULL, reader_main, r) != 0) {
    close(r->pfd[0]);
    close(r->pfd[1]);
    free(r);
    return NULL;
  }
  return r;
}

int srtsrc_fd(const srtsrc_t *r) { return r->pfd[0]; }

void srtsrc_close(srtsrc_t *r) {
  if (!r)
    return;
  atomic_store_explicit(&r->stop, 1, memory_order_relaxed);
  pthread_join(r->thread, NULL);
  close(r->pfd[0]); /* pfd[1] already closed by reader_main on exit */
  free(r);
}
