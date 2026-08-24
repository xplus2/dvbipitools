/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "srtcommon.h"
#include "srtout.h"
#include "srtsink.h"

struct srtsink {
  srtout_t *o;
};

static srtgroup_mode_t map_group_mode(srtsink_group_mode_t m) {
  switch (m) {
  case SRTSINK_GROUP_BROADCAST:
    return SRTGROUP_BROADCAST;
  case SRTSINK_GROUP_BACKUP:
    return SRTGROUP_BACKUP;
  default:
    return SRTGROUP_NONE;
  }
}

srtsink_t *srtsink_open(const srtsink_cfg_t *cfg) {
  srtsink_t *r;
  srtout_cfg_t oc;

  if (cfg->npeers <= 0 || cfg->npeers > SRTSINK_MAX_PEERS)
    return NULL;

  memset(&oc, 0, sizeof oc);
  for (int i = 0; i < cfg->npeers; i++) {
    oc.peers[i].host = cfg->peers[i].host;
    oc.peers[i].port = cfg->peers[i].port;
  }
  oc.npeers = cfg->npeers;
  oc.group_mode = map_group_mode(cfg->group_mode);
  oc.opts.passphrase = cfg->passphrase;
  oc.opts.pbkeylen = cfg->pbkeylen;
  oc.opts.streamid = cfg->streamid;
  oc.opts.packetfilter = cfg->packetfilter;
  oc.opts.latency_ms = cfg->latency_ms;
  oc.verbose = cfg->verbose;
  oc.mx = cfg->mx;
  oc.tool_version = cfg->tool_version;
  oc.safety_mult = cfg->safety_mult;

  r = calloc(1, sizeof *r);
  if (!r)
    return NULL;
  r->o = srtout_open(&oc);
  if (!r->o) {
    free(r);
    return NULL;
  }
  return r;
}

void srtsink_service(srtsink_t *r, srtsink_status_t *out) {
  srtout_status_t st;
  srtout_service(r->o, &st);
  out->connected = st.connected;
}

void srtsink_write(srtsink_t *r, const unsigned char *buf, size_t n) { srtout_write(r->o, buf, n); }

void srtsink_close(srtsink_t *r) {
  if (!r)
    return;
  srtout_close(r->o);
  free(r);
}
