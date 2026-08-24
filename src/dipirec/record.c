/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "lib/demux/psi/psi.h"
#include "lib/log.h"
#include "lib/signal.h"

#include "record/priv.h"

#define DIPIREC_RIST_DRAIN_MS_DEFAULT 1000 /* librist's own default recovery buffer length */
#define DIPIREC_SRT_DRAIN_MS_DEFAULT 1000  /* srt's own default latency buffer */

int record_run(const config_t *cfg, metrics_exporter_t *mx) {
  unsigned long long bytes = 0;
  double start;
  src_t s;
  int have_src = 0;
  out_sink_t sinks[DIPIREC_MAX_OUT];
  int n_sinks = 0;
  int mkv_fd = -1;
  rtmp_fanout_t rf;
  int is_mkv_fmt = (cfg->format == FMT_MKV || cfg->format == FMT_MKA);
  int rc = 0;
  unsigned pmt_pid, all_pids[PSI_MAX_PROGRAMS];
  int n_all_pids;
  pace_ctrl_t *pace;

  rf.n = 0;
  for (int i = 0; i < cfg->n_out && !rc; i++) {
    if (cfg->out[i].kind == OUT_RTMP || cfg->out[i].kind == OUT_RTMPS)
      continue;
    if (is_mkv_fmt) {
      mkv_fd = open_output(cfg->out[i].file_path);
      rc = mkv_fd < 0;
      continue;
    }
    rc = sink_open(cfg, &cfg->out[i], &sinks[n_sinks]);
    if (!rc)
      n_sinks++;
  }
  if (!rc)
    rc = rtmp_fanout_open(cfg, &rf);
  if (!rc)
    rc = src_open(cfg, &s);
  have_src = !rc;
  if (!rc)
    rc = resolve_pmt_selection(cfg, &s, &pmt_pid, all_pids, &n_all_pids);

  if (rc) {
    if (have_src)
      src_close(&s);
    rtmp_fanout_close(&rf);
    for (int i = 0; i < n_sinks; i++)
      sink_close(&sinks[i]);
    if (mkv_fd >= 0 && mkv_fd != STDOUT_FILENO)
      close(mkv_fd);
    return 1;
  }

  if (cfg->source.kind == URI_FILE) /* probing above consumed bytes it can't give back */
    tssrc_rewind(s.t);

  pace = cfg->pace ? pace_new() : NULL;
  start = mono_seconds();
  if (cfg->format == FMT_RAW || (cfg->format == FMT_TS && n_all_pids > 0))
    /* -p all under -f ts: nothing left to filter, mkv/flv both need one fixed program anyway */
    rc = run_raw(&s, cfg, sinks, n_sinks, &rf, mx, &bytes, start, pace);
  else
    rc = run_stream(&s, cfg, sinks, n_sinks, mkv_fd, &rf, mx, &bytes, start, cfg->format == FMT_MKV, pmt_pid, all_pids, n_all_pids, pace);
  pace_free(pace);

  if (!stop_now(cfg, start)) {
    /* eof/err, not live stop: drain rist/srt retransmits before teardown */
    int have_rist_out = 0, have_srt_out = 0;
    for (int i = 0; i < n_sinks; i++) {
      if (sinks[i].rist)
        have_rist_out = 1;
      if (sinks[i].srt)
        have_srt_out = 1;
    }
    if (have_rist_out) {
      unsigned drain_ms = cfg->rist_buffer_ms ? cfg->rist_buffer_ms : DIPIREC_RIST_DRAIN_MS_DEFAULT;
      struct timespec ts = {(time_t)(drain_ms / 1000), (long)(drain_ms % 1000) * 1000000L};
      nanosleep(&ts, NULL);
    }
    if (have_srt_out) {
      unsigned drain_ms = cfg->srt_latency_ms ? cfg->srt_latency_ms : DIPIREC_SRT_DRAIN_MS_DEFAULT;
      struct timespec ts = {(time_t)(drain_ms / 1000), (long)(drain_ms % 1000) * 1000000L};
      nanosleep(&ts, NULL);
    }
  }

  src_close(&s); /* IGMP/MLD leave */
  rtmp_fanout_close(&rf);
  for (int i = 0; i < n_sinks; i++)
    sink_close(&sinks[i]);
  if (mkv_fd >= 0 && mkv_fd != STDOUT_FILENO)
    close(mkv_fd);

  if (cfg->verbose && log_stderr_is_tty())
    fputc('\n', stderr); /* off stats line */
  if (rc == 0) {
    log_line_ansi("recorded for \e[0;33m%.1f\e[0ms, \e[0;33m%.1f\e[0mMB written", mono_seconds() - start, (double)bytes / 1048576.0);
    log_line("done.");
  }
  return rc;
}
