/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <netdb.h>
#include <string.h>

#include "lib/ioutil.h"
#include "lib/log.h"

#include "srtcommon.h"

static void srt_log_cb(void *opaque, int level, const char *file, int line, const char *area, const char *message) {
  (void)opaque;
  (void)level;
  (void)file;
  (void)line;
  (void)area;
  log_line("srt: %s", message);
}

void srtcommon_open_logging(int verbose) {
  srt_setloglevel(verbose ? LOG_DEBUG : LOG_WARNING);
  srt_setloghandler(NULL, srt_log_cb);
}

int srtcommon_apply_opts(SRTSOCKET s, const srtcommon_opts_t *o, int is_group, int timeo_optname, int timeo_ms) {
  int transtype = SRTT_LIVE;

  if (!is_group && srt_setsockopt(s, 0, SRTO_TRANSTYPE, &transtype, sizeof transtype) != 0)
    goto fail;
  if (o->passphrase && o->passphrase[0]) {
    if (srt_setsockopt(s, 0, SRTO_PASSPHRASE, o->passphrase, (int)strlen(o->passphrase)) != 0)
      goto fail;
    if (o->pbkeylen) {
      int kl = o->pbkeylen;
      if (srt_setsockopt(s, 0, SRTO_PBKEYLEN, &kl, sizeof kl) != 0)
        goto fail;
    }
  }
  if (o->streamid && o->streamid[0] && srt_setsockopt(s, 0, SRTO_STREAMID, o->streamid, (int)strlen(o->streamid)) != 0)
    goto fail;
  if (o->packetfilter && o->packetfilter[0] &&
      srt_setsockopt(s, 0, SRTO_PACKETFILTER, o->packetfilter, (int)strlen(o->packetfilter)) != 0)
    goto fail;
  if (o->latency_ms) {
    int lat = (int)o->latency_ms;
    if (srt_setsockopt(s, 0, SRTO_LATENCY, &lat, sizeof lat) != 0)
      goto fail;
  }
  if (srt_setsockopt(s, 0, timeo_optname, &timeo_ms, sizeof timeo_ms) != 0)
    goto fail;
  return 0;

fail:
  log_line("srt: setsockopt failed: %s", srt_getlasterror_str());
  return -1;
}

int srtcommon_resolve(const char *host, unsigned port, struct sockaddr_storage *ss, int *len) {
  struct addrinfo hints, *res;
  char portbuf[6];
  int rc;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  uint_to_str(portbuf, port);
  rc = getaddrinfo(host, portbuf, &hints, &res);
  if (rc != 0) {
    log_line("srt: address resolve failed for %s:%u: %s", host, port, gai_strerror(rc));
    return -1;
  }
  memcpy(ss, res->ai_addr, res->ai_addrlen);
  *len = (int)res->ai_addrlen;
  freeaddrinfo(res);
  return 0;
}

int srtcommon_build_group_config(const srtcommon_peer_t *peers, int npeers, SRT_SOCKGROUPCONFIG *gc_out) {
  for (int i = 0; i < npeers; i++) {
    struct sockaddr_storage remote;
    int remote_len;

    if (srtcommon_resolve(peers[i].host, peers[i].port, &remote, &remote_len))
      return -1;
    gc_out[i] = srt_prepare_endpoint(NULL, (struct sockaddr *)&remote, remote_len);
  }
  return 0;
}
