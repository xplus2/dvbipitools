/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <unistd.h>
#include <srt/srt.h>

#include "lib/helper/log.h"
#include "lib/sys/signal.h"
#include "lib/net/srt/srtcommon.h"
#include "lib/net/srt/srtin.h"
#include "lib/net/srt/srtin/srtin_int.h"

SRTSOCKET open_group_caller(const srtin_cfg_t *cfg, int *transient) {
  SRTSOCKET g;
  SRT_GROUP_TYPE type = cfg->group_mode == SRTGROUP_BACKUP ? SRT_GTYPE_BACKUP : SRT_GTYPE_BROADCAST;
  SRT_SOCKGROUPCONFIG gc[SRTCOMMON_MAX_PEERS];

  if (transient) *transient = 0;
  g = srt_create_group(type);
  if (g == SRT_INVALID_SOCK) {
    log_code("LE500164", "srt: group create failed: %s", srt_getlasterror_str());
    return SRT_INVALID_SOCK;
  }
  if (srtcommon_apply_opts(g, &cfg->opts, 1, SRTO_RCVTIMEO, SRT_RCVTIMEO_MS)) {
    srt_close(g);
    return SRT_INVALID_SOCK;
  }
  if (srtcommon_build_group_config(cfg->peers, cfg->npeers, gc)) {
    srt_close(g);
    return SRT_INVALID_SOCK;
  }
  if (srt_connect_group(g, gc, cfg->npeers) == SRT_ERROR) {
    log_code("LE600139", "srt: group connect failed: %s", srt_getlasterror_str());
    srt_close(g);
    if (transient) *transient = 1;
    return SRT_INVALID_SOCK;
  }
  return g;
}

/* one listener per bonded peer. accept_bond returns first hello. GROUPCONNECT: accept
   returns group id, not socket. lifetime rule: see open_single_listener() */
static SRTSOCKET open_group_listener(const srtin_cfg_t *cfg, SRTSOCKET *listeners_out, int *n_listeners_out) {
  SRTSOCKET listeners[SRTCOMMON_MAX_PEERS];
  int groupconnect = 1;
  SRTSOCKET s = SRT_INVALID_SOCK;
  int i;

  *n_listeners_out = 0;
  for (i = 0; i < cfg->npeers; i++) {
    struct sockaddr_storage local;
    int local_len;

    listeners[i] = srt_create_socket();
    if (listeners[i] == SRT_INVALID_SOCK) {
      log_code("LE500159", "srt: socket create failed: %s", srt_getlasterror_str());
      goto fail;
    }
    if (srtcommon_apply_opts(listeners[i], &cfg->opts, 0, SRTO_RCVTIMEO, SRT_RCVTIMEO_MS) ||
        srt_setsockopt(listeners[i], 0, SRTO_GROUPCONNECT, &groupconnect, sizeof groupconnect) != 0) {
      log_code("LE500158", "srt: setsockopt failed: %s", srt_getlasterror_str());
      goto fail;
    }
    if (srtcommon_resolve(cfg->peers[i].host, cfg->peers[i].port, &local, &local_len))
      goto fail;
    if (srt_bind(listeners[i], (struct sockaddr *)&local, local_len) != 0 || srt_listen(listeners[i], 1) != 0) {
      log_code("LE500165", "srt: bind/listen failed on peer %d: %s", i, srt_getlasterror_str());
      goto fail;
    }
  }
  while (!stop_wanted(cfg)) {
    s = srt_accept_bond(listeners, cfg->npeers, SRT_RCVTIMEO_MS);
    if (s != SRT_INVALID_SOCK)
      break;
    if (srt_getlasterror(NULL) != SRT_ETIMEOUT) {
      log_code("LE600140", "srt: accept_bond failed: %s", srt_getlasterror_str());
      s = SRT_INVALID_SOCK;
      break;
    }
  }
  if (s == SRT_INVALID_SOCK) {
    for (int j = 0; j < i; j++)
      srt_close(listeners[j]);
  } else {
    memcpy(listeners_out, listeners, (size_t)i * sizeof listeners[0]);
    *n_listeners_out = i;
  }
  return s;

fail:
  for (int j = 0; j < i; j++)
    srt_close(listeners[j]);
  return SRT_INVALID_SOCK;
}

/* listeners_out/n_listeners_out: sockets outliving this call, see open_single_listener().
   caller flavors: empty, nothing to keep alive */
SRTSOCKET open_srt_input(const srtin_cfg_t *cfg, SRTSOCKET *listeners_out, int *n_listeners_out, atomic_int *busy, int *transient) {
  *n_listeners_out = 0;
  *transient = 0;
  if (cfg->group_mode != SRTGROUP_NONE)
    return cfg->listen ? open_group_listener(cfg, listeners_out, n_listeners_out) : open_group_caller(cfg, transient);
  if (cfg->listen) {
    SRTSOCKET lsn;
    SRTSOCKET s = open_single_listener(cfg, &lsn, busy);
    if (lsn != SRT_INVALID_SOCK) {
      listeners_out[0] = lsn;
      *n_listeners_out = 1;
    }
    return s;
  }
  return open_single_caller(cfg, transient);
}
