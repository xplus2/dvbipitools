/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <time.h>

#include "lib/log.h"
#include "lib/net/srt/srtin.h"
#include "lib/net/srt/srtout.h"
#include "lib/net/tssink.h"
#include "lib/net/tssource.h"
#include "lib/signal.h"

#include "args.h"
#include "bridge.h"
#include "version.h"

#define DIPISRT_SENDER_POLL_MS 200
#define DIPISRT_SENDER_DRAIN_MS_DEFAULT 1000 /* srt's own default latency buffer */

void nonsrt_to_tssrc_cfg(const nonsrt_t *s, const char *iface, int insecure_tls, tssrc_cfg_t *tc) {
  memset(tc, 0, sizeof *tc);
  tc->user_agent = TOOL_NAME "/" TOOL_VERSION;
  switch (s->kind) {
  case NONSRT_HTTP:
    tc->kind = TSSRC_HTTP;
    tc->http = s->http;
    tc->insecure_tls = insecure_tls;
    break;
  case NONSRT_FILE:
    if (s->file_path[0]) {
      tc->kind = TSSRC_FILE;
      tc->file_path = s->file_path;
    } else {
      tc->kind = TSSRC_STDIN;
    }
    break;
  case NONSRT_RTP:
  case NONSRT_UDP:
    tc->kind = (s->kind == NONSRT_RTP) ? TSSRC_RTP : TSSRC_UDP;
    tc->family = s->family;
    tc->group = s->group;
    tc->port = s->port;
    tc->iface = iface;
    break;
  }
}

void nonsrt_to_tssink_cfg(const nonsrt_t *s, const char *iface, tssink_cfg_t *tk) {
  memset(tk, 0, sizeof *tk);
  if (s->kind == NONSRT_FILE) {
    if (s->file_path[0]) {
      tk->kind = TSSINK_FILE;
      tk->file_path = s->file_path;
    } else {
      tk->kind = TSSINK_STDOUT;
    }
  } else {
    tk->kind = (s->kind == NONSRT_RTP) ? TSSINK_RTP : TSSINK_UDP;
    tk->family = s->family;
    tk->group = s->group;
    tk->port = s->port;
    tk->iface = iface;
  }
}

static void fill_common_opts(const config_t *cfg, srtcommon_opts_t *o) {
  o->passphrase = cfg->passphrase[0] ? cfg->passphrase : NULL;
  o->pbkeylen = cfg->pbkeylen;
  o->streamid = cfg->streamid[0] ? cfg->streamid : NULL;
  o->packetfilter = cfg->packetfilter[0] ? cfg->packetfilter : NULL;
  o->latency_ms = cfg->latency_ms;
}

static int run_sender(const config_t *cfg, metrics_exporter_t *mx) {
  tssrc_cfg_t tc;
  tssrc_t *src;
  srtout_cfg_t rcfg;
  srtout_t *srt;
  struct pollfd pfd;
  unsigned char buf[65536];
  int rc = 0;
  int was_connected = 0;

  nonsrt_to_tssrc_cfg(&cfg->in.nonsrt, cfg->iface, cfg->insecure_tls, &tc);
  src = tssrc_open(&tc, NULL);
  if (!src)
    return 1;

  memset(&rcfg, 0, sizeof rcfg);
  for (int i = 0; i < cfg->out.n_srt; i++) {
    rcfg.peers[i].host = cfg->out.srt_host[i];
    rcfg.peers[i].port = cfg->out.srt_port[i];
  }
  rcfg.npeers = cfg->out.n_srt;
  rcfg.group_mode = cfg->group_mode;
  rcfg.rendezvous = cfg->rendezvous;
  rcfg.local_host = cfg->local_host[0] ? cfg->local_host : NULL;
  rcfg.local_port = cfg->local_port;
  fill_common_opts(cfg, &rcfg.opts);
  rcfg.verbose = cfg->verbose;
  rcfg.mx = mx;
  rcfg.tool_version = TOOL_VERSION;
  rcfg.safety_mult = cfg->send_buffer_mult;

  srt = srtout_open(&rcfg);
  if (!srt) {
    tssrc_close(src);
    return 1;
  }

  pfd.fd = tssrc_fd(src);
  pfd.events = POLLIN;

  while (!signal_stop_requested()) {
    srtout_status_t st;
    int pr;

    srtout_service(srt, &st);
    if (st.connected != was_connected) {
      log_line(st.connected ? "srt output: connected" : "srt output: link down, reconnecting");
      was_connected = st.connected;
    }

    pr = poll(&pfd, 1, DIPISRT_SENDER_POLL_MS);
    if (pr < 0) {
      if (errno == EINTR)
        continue;
      log_line("srt output: poll failed: %s", strerror(errno));
      rc = 1;
      break;
    }
    if (pr == 0 || !(pfd.revents & (POLLIN | POLLERR | POLLHUP)))
      continue;

    {
      net_err_reason_t reason = NET_ERR_OTHER;
      ssize_t n = tssrc_read(src, buf, sizeof buf, &reason);

      if (n < 0) {
        rc = 1;
        break;
      }
      if (n > 0)
        srtout_write(srt, buf, (size_t)n);
    }
  }
  if (rc) {
    /* eof/err, not live stop: drain srt retransmits before teardown */
    unsigned drain_ms = cfg->latency_ms ? cfg->latency_ms : DIPISRT_SENDER_DRAIN_MS_DEFAULT;
    struct timespec ts = {(time_t)(drain_ms / 1000), (long)(drain_ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
  }
  srtout_close(srt);
  tssrc_close(src);
  return rc;
}

#define RECV_DEDUP_HISTORY 6 /* reconnect can redeliver several already-written chunks */

static int run_receiver(const config_t *cfg, metrics_exporter_t *mx) {
  tssink_cfg_t tk;
  tssink_t *sink;
  srtin_cfg_t rcfg;
  srtin_t *srt;
  unsigned char buf[65536];
  unsigned char dedup_hist[RECV_DEDUP_HISTORY][65536];
  int dedup_hist_len[RECV_DEDUP_HISTORY] = {0};
  int dedup_hist_next = 0;
  int dedup_active = 0; /* set on reconnect, cleared at first non-duplicate chunk */
  int rc = 0;

  nonsrt_to_tssink_cfg(&cfg->out.nonsrt, cfg->iface, &tk);
  sink = tssink_open(&tk);
  if (!sink)
    return 1;

  memset(&rcfg, 0, sizeof rcfg);
  for (int i = 0; i < cfg->in.n_srt; i++) {
    rcfg.peers[i].host = cfg->in.srt_host[i];
    rcfg.peers[i].port = cfg->in.srt_port[i];
  }
  rcfg.npeers = cfg->in.n_srt;
  rcfg.group_mode = cfg->group_mode;
  rcfg.listen = cfg->in.listen;
  rcfg.rendezvous = cfg->rendezvous;
  rcfg.local_host = cfg->local_host[0] ? cfg->local_host : NULL;
  rcfg.local_port = cfg->local_port;
  fill_common_opts(cfg, &rcfg.opts);
  rcfg.verbose = cfg->verbose;
  rcfg.mx = mx;
  rcfg.tool_version = TOOL_VERSION;

  srt = srtin_open(&rcfg);
  if (!srt) {
    tssink_close(sink);
    return signal_stop_requested() ? 0 : 1;
  }

  while (!signal_stop_requested()) {
    int reconnected = 0;
    int n = srtin_read(srt, buf, sizeof buf, &reconnected);

    if (n < 0) {
      rc = 1;
      break;
    }
    if (reconnected)
      dedup_active = 1;
    if (n == 0)
      continue;
    if (dedup_active) {
      int is_dup = 0;
      for (int h = 0; h < RECV_DEDUP_HISTORY; h++) {
        if (dedup_hist_len[h] == n && memcmp(buf, dedup_hist[h], (size_t)n) == 0) {
          is_dup = 1;
          break;
        }
      }
      if (is_dup)
        continue;
      dedup_active = 0;
    }
    if (tssink_write(sink, buf, (size_t)n) < 0) {
      rc = 1;
      break;
    }
    memcpy(dedup_hist[dedup_hist_next], buf, (size_t)n);
    dedup_hist_len[dedup_hist_next] = n;
    dedup_hist_next = (dedup_hist_next + 1) % RECV_DEDUP_HISTORY;
  }

  srtin_close(srt);
  tssink_close(sink);
  return rc;
}

int bridge_run(const config_t *cfg, metrics_exporter_t *mx) {
  return config_is_sender(cfg) ? run_sender(cfg, mx) : run_receiver(cfg, mx);
}
