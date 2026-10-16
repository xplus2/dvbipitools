/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/crc32.h"
#include "lib/demux/psi/psi.h"
#include "lib/demux/tspack.h"
#include "lib/helper/log.h"
#include "lib/net/send_result.h"
#include "lib/net/srt/srtin.h"
#include "lib/net/srt/srtout.h"
#include "lib/net/ts/pace.h"
#include "lib/net/ts/sink.h"
#include "lib/net/ts/source.h"
#include "lib/tsinspect/inspect.h"
#include "lib/sys/signal.h"

#include "cli/args.h"
#include "bridge.h"
#include "version.h"

#define DIPISRT_SENDER_POLL_MS 200
#define DIPISRT_SENDER_READS_MAX 64
#define DIPISRT_PACED_READ_CAP (7 * 188) /* bounds sleep per chunk */
#define DIPISRT_STATS_INTERVAL_S 5.0
#define DIPISRT_OPEN_TIMEOUT_S 30.0


typedef struct {
  psi_t *psi;
  pace_ctrl_t *pace;
} pace_ctx_t;

static int pace_pkt_cb(void *v, const unsigned char *pkt) {
  pace_ctx_t *c = v;
  pace_feed_pcr_pkt(c->pace, pkt, psi_pcr_pid(c->psi));
  psi_feed(c->psi, pkt);
  return 0;
}

static int insp_pkt_cb(void *v, const unsigned char *pkt) {
  tsinspect_packet(v, pkt);
  return 0;
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
  tsinspect_t *insp;
  tsinspect_set_t set = {&insp, 1, NULL, 0};
  int fec_fd;
  int drain;
  unsigned char buf[65536];
  int rc = 0;
  int was_connected = 0;
  pace_ctx_t pc = {NULL, NULL};
  tspack_t pz = {{0}, 0, 0};
  size_t read_cap = sizeof buf;
  double last_stat = mono_seconds();

  plain_endpoint_to_tssrc_cfg(&cfg->in.nonsrt, cfg->iface, TOOL_NAME "/" TOOL_VERSION, cfg->insecure_tls, &tc);
  src = tssrc_open_blocking(&tc, DIPISRT_OPEN_TIMEOUT_S, NULL);
  if (!src) return 1;

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
  rcfg.queue_metrics = metrics_queue_level(cfg->metrics_inspect_ts);

  srt = srtout_open(&rcfg);
  if (!srt) {
    tssrc_close(src);
    return 1;
  }

  if (cfg->pace) {
    pc.pace = pace_new();
    pc.psi = psi_new();
    if (!pc.pace || !pc.psi) {
      log_code("HS500101", "out of memory");
      psi_free(pc.psi);
      pace_free(pc.pace);
      srtout_close(srt);
      tssrc_close(src);
      return 1;
    }
    read_cap = DIPISRT_PACED_READ_CAP;
  }

  insp = tsinspect_new_relay(cfg->metrics_inspect_ts);
  if (insp) metrics_exporter_set_extra(mx, tsinspect_set_put, &set);
  if (tsinspect_wants_rx_ns(insp)) tssrc_enable_rx_timestamps(src);

  fec_fd = tssrc_fec_fd(src);
  drain = fec_fd >= 0 || tssrc_set_nonblock(src);

  while (!signal_stop_requested()) {
    srtout_status_t st;
    int pr;
    net_err_reason_t reason = NET_ERR_OTHER;
    ssize_t n;

    srtout_service(srt, &st);
    if (cfg->verbose && mono_seconds() - last_stat >= DIPISRT_STATS_INTERVAL_S) {
      srtout_link_stats_t ls;
      srtout_link_stats(srt, &ls);
      last_stat = mono_seconds();
      if (ls.connected)
        log_line("srt out: %.2f Mbit/s, %llu pkts, %llu retrans, %llu lost, %llu dropped, rtt %.1f ms", ls.mbps,
                 (unsigned long long)ls.packets, (unsigned long long)ls.retransmitted, (unsigned long long)ls.lost,
                 (unsigned long long)ls.dropped, ls.rtt_ms);
    }
    if (insp) tsinspect_tick(insp, mono_seconds());
    if (st.connected != was_connected) {
      if (st.connected) log_line("srt output: connected");
      else              log_code("HW600103", "srt output: link down, reconnecting");
      was_connected = st.connected;
    }

    pr = tssrc_wait(src, DIPISRT_SENDER_POLL_MS);
    if (pr < 0) {
      log_code("HE500115", "srt output: poll failed: %s", strerror(errno));
      rc = 1;
      break;
    }
    if (pr == 0) continue;
    if (fec_fd >= 0) tssrc_fec_poll(src);

    for (unsigned reads = 0; reads < DIPISRT_SENDER_READS_MAX; reads++) {
      n = tssrc_read(src, buf, read_cap, &reason);
      if (n <= 0) break;
      if (pc.pace) {
        if (tssrc_is_rtp_framed(src)) pace_feed_rtp_ts(pc.pace, tssrc_last_rtp_ts(src));
        else tspack_feed(&pz, buf, (size_t)n, pace_pkt_cb, &pc);
      }
      if (insp) {
        tsinspect_set_rx_ns(insp, tssrc_last_rx_ns(src));
        tsinspect_grid(insp, buf, (size_t)n);
      }
      srtout_write(srt, buf, (size_t)n);
      if (!drain) break;
    }
    if (n < 0) {
      rc = reason == NET_ERR_EOF ? 0 : 1;
      break;
    }
  }
  if (insp) metrics_exporter_set_extra(mx, NULL, NULL);
  tsinspect_free(insp);
  if (signal_stop_requested()) srtout_close_now(srt);
  else srtout_close(srt);
  tssrc_close(src);
  psi_free(pc.psi);
  pace_free(pc.pace);
  return rc;
}

#define RECV_DEDUP_HISTORY 6 /* reconnect can redeliver several already-written chunks */

void dedup_replay_begin(dedup_replay_t *r, int hist_n) {
  r->cand = hist_n >= 32 ? 0xFFFFFFFFu : (1u << hist_n) - 1;
  r->pos = 0;
}

int dedup_replay_match(dedup_replay_t *r, const dedup_entry_t *hist, int hist_n, int next, uint32_t hash, int len) {
  uint32_t keep = 0;
  int done = 0;
  for (int s = 0; s < hist_n; s++) {
    const dedup_entry_t *e;
    if (!(r->cand & (1u << s)) || s + r->pos >= hist_n) continue;
    e = &hist[(next + s + r->pos) % hist_n];
    if (e->len == len && e->len > 0 && e->hash == hash) keep |= 1u << s;
  }
  r->cand = keep;
  if (!keep) return 0;
  r->pos++;
  for (int s = 0; s < hist_n; s++) {
    if ((keep & (1u << s)) && s + r->pos >= hist_n) done = 1;
  }
  if (done) r->cand = 0;
  return 1;
}

void dedup_record(dedup_entry_t *hist, int hist_n, int *next, uint32_t hash, int len) {
  hist[*next].hash = hash;
  hist[*next].len = len;
  *next = (*next + 1) % hist_n;
}

static int run_receiver(const config_t *cfg, metrics_exporter_t *mx) {
  tssink_cfg_t tk;
  tssink_t *sink;
  srtin_cfg_t rcfg;
  srtin_t *srt;
  tsinspect_t *insp_in;
  tsinspect_t *insp_out;
  tsinspect_set_t set = {&insp_in, 1, &insp_out, 1};
  unsigned char buf[65536];
  dedup_entry_t dedup_hist[RECV_DEDUP_HISTORY] = {{0, 0}};
  int dedup_hist_next = 0;
  tspack_t pz_in = {{0}, 0, 0};
  tspack_t pz_out = {{0}, 0, 0};
  dedup_replay_t dedup_replay = {0, 0}; /* armed on reconnect, cand 0 when idle */
  int net_had_error = 0;
  unsigned long long net_errors = 0;
  int net_retry;
  int rc = 0;
  double last_stat = mono_seconds();

  plain_endpoint_to_tssink_cfg(&cfg->out.nonsrt, cfg->iface, &tk);
  tk.pack = 1;
  net_retry = tk.kind == TSSINK_UDP || tk.kind == TSSINK_RTP;
  sink = tssink_open(&tk);
  if (!sink) {
    return 1;
  }

  memset(&rcfg, 0, sizeof rcfg);
  for (int i = 0; i < cfg->in.n_srt; i++) {
    rcfg.peers[i].host = cfg->in.srt_host[i];
    rcfg.peers[i].port = cfg->in.srt_port[i];
  }
  rcfg.npeers = cfg->in.n_srt;
  rcfg.group_mode = cfg->group_mode;
  rcfg.listen = cfg->in.listen;
  rcfg.retry_open = 1;
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

  insp_in = tsinspect_new_relay(cfg->metrics_inspect_ts);
  insp_out = tsinspect_new_relay(cfg->metrics_inspect_ts);
  if (insp_in) metrics_exporter_set_extra(mx, tsinspect_set_put, &set);

  while (!signal_stop_requested()) {
    int reconnected = 0;
    int n = srtin_read(srt, buf, sizeof buf, &reconnected);
    if (cfg->verbose && mono_seconds() - last_stat >= DIPISRT_STATS_INTERVAL_S) {
      srtin_stats_t ls;
      srtin_stats(srt, &ls);
      last_stat = mono_seconds();
      if (ls.connected)
        log_line("srt in: %.2f Mbit/s, %llu pkts, %llu lost, %llu dropped, rtt %.1f ms", ls.mbps,
                 (unsigned long long)ls.packets, (unsigned long long)ls.lost, (unsigned long long)ls.dropped, ls.rtt_ms);
    }
    uint32_t h;
    int wr;
    if (insp_in) {
      double now = mono_seconds();
      tsinspect_tick(insp_in, now);
      if (insp_out) tsinspect_tick(insp_out, now);
    }

    if (n < 0) {
      rc = 1;
      break;
    }
    if (reconnected) dedup_replay_begin(&dedup_replay, RECV_DEDUP_HISTORY);
    if (n == 0) {
      tssink_flush(sink);
      continue;
    }
    if (insp_in) tspack_feed_sync(&pz_in, buf, (size_t)n, insp_pkt_cb, insp_in, tsinspect_sync(insp_in));
    h = crc32_mpeg(buf, (size_t)n);
    if (dedup_replay.cand && dedup_replay_match(&dedup_replay, dedup_hist, RECV_DEDUP_HISTORY, dedup_hist_next, h, n))
      continue;
    wr = tssink_write(sink, buf, (size_t)n);
    if (wr < 0 && !net_retry) {
      rc = 1;
      break;
    }
    note_send_result(wr >= 0, &net_had_error, &net_errors, "net");
    if (wr >= 0 && insp_out) tspack_feed_sync(&pz_out, buf, (size_t)n, insp_pkt_cb, insp_out, tsinspect_sync(insp_out));
    dedup_record(dedup_hist, RECV_DEDUP_HISTORY, &dedup_hist_next, h, n);
  }

  if (insp_in) metrics_exporter_set_extra(mx, NULL, NULL);
  tsinspect_free(insp_in);
  tsinspect_free(insp_out);
  srtin_close(srt);
  tssink_close(sink);
  return rc;
}

int bridge_run(const config_t *cfg, metrics_exporter_t *mx) {
  return config_is_sender(cfg) ? run_sender(cfg, mx) : run_receiver(cfg, mx);
}
