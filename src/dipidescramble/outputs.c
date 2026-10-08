/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "lib/helper/log.h"
#include "lib/metrics/export.h"

#include "outputs.h"
#include "version.h"

static int open_output(const char *path) {
  int fd;
  if (strcmp(path, "-") == 0) return STDOUT_FILENO;
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0640);
  if (fd < 0) log_line(TOOL_NAME ": cannot open -o %s: %s", path, strerror(errno));
  return fd;
}

/* opens every -o target: plain files into lc->outfd[], rtmp(s) targets into lc->rtmp[]. 0 ok, -1 fail */
int dscr_open_outputs(const config_t *cfg, loop_ctx_t *lc, int *mkv_fd) {
  int is_mkv_fmt = (cfg->format == FMT_MKV || cfg->format == FMT_MKA);
  lc->n_outfd = 0;
  lc->n_rtmp = 0;
  lc->n_srt = 0;
  *mkv_fd = -1;
  for (int i = 0; i < cfg->n_out; i++) {
    const out_target_t *o = &cfg->out[i];
    switch (o->kind) {
      case OUT_RTMP:
      case OUT_RTMPS: {
        rtmpout_cfg_t rc;
        memset(&rc, 0, sizeof rc);
        rc.url = o->rtmp_url;
        rc.insecure = cfg->insecure_tls;
        lc->rtmp[lc->n_rtmp] = rtmpout_open(&rc);
        if (!lc->rtmp[lc->n_rtmp]) return -1;
        lc->rtmp_had_error[lc->n_rtmp] = 0;
        lc->n_rtmp++;
        break;
      }
      case OUT_SRT: {
        srtsink_cfg_t sc;
        memset(&sc, 0, sizeof sc);
        sc.peers[0].host = o->srt_host;
        sc.peers[0].port = o->srt_port;
        sc.npeers = 1;
        sc.group_mode = SRTSINK_GROUP_NONE;
        sc.passphrase = cfg->srt_passphrase;
        sc.pbkeylen = cfg->srt_pbkeylen;
        sc.streamid = cfg->srt_streamid;
        sc.packetfilter = cfg->srt_packetfilter;
        sc.latency_ms = cfg->srt_latency_ms;
        sc.verbose = cfg->verbose;
        sc.queue_metrics = metrics_queue_level(cfg->metrics_inspect_ts);
        lc->srt[lc->n_srt] = srtsink_open(&sc);
        if (!lc->srt[lc->n_srt]) return -1;
        lc->srt_connected[lc->n_srt] = 0;
        lc->n_srt++;
        break;
      }
      case OUT_FILE:
        if (is_mkv_fmt) {
          *mkv_fd = open_output(o->file_path);
          if (*mkv_fd < 0) return -1;
          break;
        }
        lc->outfd[lc->n_outfd] = open_output(o->file_path);
        if (lc->outfd[lc->n_outfd] < 0) return -1;
        lc->n_outfd++;
        break;
    }
  }
  return 0;
}

void dscr_close_outputs(const loop_ctx_t *lc, int mkv_fd) {
  for (int i = 0; i < lc->n_rtmp; i++) rtmpout_close(lc->rtmp[i]);
  for (int i = 0; i < lc->n_srt; i++) srtsink_close(lc->srt[i]);
  for (int i = 0; i < lc->n_outfd; i++) {
    if (lc->outfd[i] != STDOUT_FILENO) close(lc->outfd[i]);
  }
  if (mkv_fd >= 0 && mkv_fd != STDOUT_FILENO) close(mkv_fd);
}
