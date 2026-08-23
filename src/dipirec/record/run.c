/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "lib/demux/mpts_probe.h"
#include "lib/demux/psi/psi.h"
#include "lib/demux/tspack.h"
#include "lib/log.h"
#include "lib/mux/mkv/mkv.h"
#include "lib/signal.h"

#include "../filter/ts.h"
#include "../version.h"
#include "priv.h"

#define MPTS_NAME_WAIT_MS 3000

int stop_now(const config_t *cfg, double start) {
  if (signal_stop_requested())
    return 1;
  return cfg->duration_s && mono_seconds() - start >= (double)cfg->duration_s;
}

typedef struct {
  psi_t *psi;        /* NULL unless -v or --pace */
  pace_ctrl_t *pace;  /* NULL unless --pace */
  int pace_pcr;       /* 1: this chunk wasn't RTP-framed, pace per packet */
} raw_ctx_t;

static int raw_cb(void *v, const unsigned char *pkt) {
  raw_ctx_t *r = v;
  if (r->pace_pcr)
    pace_feed_pcr_pkt(r->pace, pkt, psi_pcr_pid(r->psi));
  if (r->psi)
    psi_feed(r->psi, pkt);
  return 0;
}

/* no flv/rtmp path here, args.c already rejects -f raw + an rtmp(s) target */
int run_raw(src_t *s, const config_t *cfg, out_sink_t *sinks, int n_sinks, const rtmp_fanout_t *rf,
            metrics_exporter_t *mx, unsigned long long *bytes, double start, pace_ctrl_t *pace) {
  unsigned char buf[65536];
  tspack_t pz = {{0}, 0};
  raw_ctx_t ctx;
  double last_stat = 0;
  int rc = 0;

  ctx.psi = (cfg->verbose || pace) ? psi_new() : NULL;
  ctx.pace = pace;
  ctx.pace_pcr = 0;

  while (!stop_now(cfg, start)) {
    ssize_t n = src_read(s, buf, sizeof buf);
    if (n < 0)
      break;
    if (n == 0)
      continue;
    if (pace) {
      if (tssrc_is_rtp_framed(s->t)) {
        pace_feed_rtp_ts(pace, tssrc_last_rtp_ts(s->t));
        ctx.pace_pcr = 0;
      } else {
        ctx.pace_pcr = 1;
      }
    }
    if (ctx.psi)
      tspack_feed(&pz, buf, (size_t)n, raw_cb, &ctx);
    for (int i = 0; i < n_sinks; i++)
      if (sink_write(&sinks[i], buf, (size_t)n)) {
        rc = 1;
        break;
      }
    if (rc)
      break;
    *bytes += (unsigned long long)n;
    if (cfg->verbose && mono_seconds() - last_stat >= 1.0) {
      stats_show(cfg, mono_seconds() - start, *bytes, ctx.psi);
      last_stat = mono_seconds();
    }
    if (metrics_exporter_enabled(mx))
      push_metrics(mx, cfg, sinks, n_sinks, rf, *bytes, start);
  }
  psi_free(ctx.psi);
  return rc;
}

typedef struct {
  ts_filter_t *f; /* NULL: no plain ts/rtp/udp/rist sink to feed */
  out_sink_t *sinks;
  int n_sinks;
  mkv_t *m;   /* NULL unless -f mkv/mka */
  flv_t *flv; /* NULL: no rtmp(s) target */
  unsigned long long *bytes;
  int bad;
  pace_ctrl_t *pace; /* NULL unless --pace */
  int pace_pcr;       /* 1: this chunk wasn't RTP-framed, pace per packet */
} stream_ctx_t;

static int write_to_sinks(out_sink_t *sinks, int n_sinks, const unsigned char *buf, size_t len) {
  for (int i = 0; i < n_sinks; i++)
    if (sink_write(&sinks[i], buf, len))
      return 1;
  return 0;
}

static int stream_cb(void *v, const unsigned char *pkt) {
  stream_ctx_t *c = v;

  if (c->pace_pcr) {
    const psi_t *p;
    if (c->f)
      p = ts_filter_psi(c->f);
    else if (c->m)
      p = mkv_psi(c->m);
    else
      p = flv_psi(c->flv);
    pace_feed_pcr_pkt(c->pace, pkt, psi_pcr_pid(p));
  }
  if (c->f) {
    unsigned char o[188];
    const unsigned char *r = ts_filter_packet(c->f, pkt, o);
    if (r) {
      if (write_to_sinks(c->sinks, c->n_sinks, r, 188))
        return 1;
      *c->bytes += 188;
    }
    if (ts_filter_bad_track(c->f)) {
      c->bad = 1;
      return 1;
    }
  }
  if (c->m) {
    mkv_feed(c->m, pkt);
    if (mkv_error(c->m))
      return 1;
  }
  if (c->flv) {
    flv_feed(c->flv, pkt);
    if (flv_error(c->flv))
      return 1;
  }
  return 0;
}

/* ts and/or mkv/mka sinks, plus an optional flv+rtmp fan-out, one shared packet feed.
   mkv_fd < 0: no mkv/mka target. rf->n == 0: no rtmp(s) target.
   pmt_pid/all_pids/n_all_pids: as resolve_pmt_selection filled them in */
int run_stream(src_t *s, const config_t *cfg, out_sink_t *sinks, int n_sinks, int mkv_fd, rtmp_fanout_t *rf,
               metrics_exporter_t *mx, unsigned long long *bytes, double start, int video_ok, unsigned pmt_pid,
               const unsigned *all_pids, int n_all_pids, pace_ctrl_t *pace) {
  unsigned char buf[65536];
  tspack_t pz = {{0}, 0};
  stream_ctx_t ctx;
  double last_stat = 0;
  int is_mkv = mkv_fd >= 0;
  int rc = 0;

  memset(&ctx, 0, sizeof ctx);
  ctx.sinks = sinks;
  ctx.n_sinks = n_sinks;
  ctx.bytes = bytes;
  ctx.pace = pace;

  if (n_sinks > 0 && !is_mkv) {
    ctx.f = ts_filter_new(cfg->audio_all, cfg->audio_track, cfg->subs == SUB_STRIP, pmt_pid, cfg->strip_mask);
    if (!ctx.f)
      return 1;
  }
  if (is_mkv) {
    mkv_opts_t opts;
    char app_name[64];
    char srcuri[1024];
    snprintf(app_name, sizeof app_name, "%s %s", TOOL_NAME, TOOL_VERSION);
    source_describe(&cfg->source, srcuri, sizeof srcuri);
    memset(&opts, 0, sizeof opts);
    opts.audio_all = n_all_pids > 0 ? 1 : cfg->audio_all; /* -p all: no single "-a N" across programs */
    opts.audio_track = cfg->audio_track;
    opts.subs_srt = (cfg->subs == SUB_SRT);
    opts.sub_lead_ms = cfg->sub_lead_ms;
    opts.app_name = app_name;
    opts.source_desc = srcuri;
    if (n_all_pids > 0)
      ctx.m = mkv_new(mkv_fd, &opts, video_ok, bytes, all_pids, n_all_pids);
    else if (pmt_pid)
      ctx.m = mkv_new(mkv_fd, &opts, video_ok, bytes, &pmt_pid, 1);
    else
      ctx.m = mkv_new(mkv_fd, &opts, video_ok, bytes, NULL, 0);
    if (!ctx.m) {
      ts_filter_free(ctx.f);
      return 1;
    }
  }
  if (rf->n > 0) {
    flv_opts_t fo;
    memset(&fo, 0, sizeof fo);
    fo.audio_track = cfg->audio_all ? 0 : cfg->audio_track;
    ctx.flv = flv_new(&fo, pmt_pid, rtmp_fanout_cb, rf, bytes);
    if (!ctx.flv) {
      mkv_close(ctx.m);
      ts_filter_free(ctx.f);
      return 1;
    }
  }

  while (!stop_now(cfg, start)) {
    ssize_t n = src_read(s, buf, sizeof buf);
    if (n < 0)
      break;
    if (n == 0)
      continue;
    if (pace) {
      if (tssrc_is_rtp_framed(s->t)) {
        pace_feed_rtp_ts(pace, tssrc_last_rtp_ts(s->t));
        ctx.pace_pcr = 0;
      } else {
        ctx.pace_pcr = 1;
      }
    }
    if (tspack_feed(&pz, buf, (size_t)n, stream_cb, &ctx)) {
      if (ctx.bad)
        log_line_ansi("audio track \e[0;31m%u\e[0;33m not found (\e[0;33m%d\e[0;33m present)", cfg->audio_track, psi_audio_count(ts_filter_psi(ctx.f)));
      rc = 1;
      break;
    }
    if (cfg->verbose && mono_seconds() - last_stat >= 1.0) {
      const psi_t *p;
      if (ctx.f)
        p = ts_filter_psi(ctx.f);
      else if (ctx.m)
        p = mkv_psi(ctx.m);
      else
        p = flv_psi(ctx.flv);
      stats_show(cfg, mono_seconds() - start, *bytes, p);
      last_stat = mono_seconds();
    }
    if (metrics_exporter_enabled(mx))
      push_metrics(mx, cfg, sinks, n_sinks, rf, *bytes, start);
  }
  if (ctx.flv)
    flv_close(ctx.flv);
  if (ctx.m)
    mkv_close(ctx.m);
  ts_filter_free(ctx.f);
  return rc;
}

static int cfg_has_rtmp(const config_t *cfg) {
  for (int i = 0; i < cfg->n_out; i++)
    if (cfg->out[i].kind == OUT_RTMP || cfg->out[i].kind == OUT_RTMPS)
      return 1;
  return 0;
}

/* mpts discovery + -p decision. 0: proceed (pmt_pid/all_pids/n_all_pids filled in).
   1: abort, message already printed. raw skips this, nothing to select there. */
int resolve_pmt_selection(const config_t *cfg, src_t *s, unsigned *pmt_pid, unsigned *all_pids, int *n_all_pids) {
  mpts_probe_result_t probe;

  *pmt_pid = 0;
  *n_all_pids = 0;

  if (cfg->format == FMT_RAW) {
    if (cfg->pmt_sel != PMT_SEL_AUTO)
      log_line(TOOL_NAME ": -p has no effect with -f raw (whole stream always forwarded)");
    return 0;
  }

  probe = mpts_probe_run(s->t, MPTS_NAME_WAIT_MS);
  if (probe.kind == MPTS_PROBE_FAIL) {
    log_line(TOOL_NAME ": no PAT received, giving up");
    return 1;
  }
  if (probe.kind == MPTS_PROBE_SPTS) {
    if (cfg->pmt_sel != PMT_SEL_AUTO)
      log_line(TOOL_NAME ": -p ignored, single-program source");
    return 0;
  }

  if (cfg->pmt_sel == PMT_SEL_AUTO) {
    mpts_probe_print_programs(TOOL_NAME, &probe);
    log_line(TOOL_NAME ": MPTS source - pick one with -p <pid>, or -p all");
    return 1;
  }
  if (cfg->pmt_sel == PMT_SEL_ALL) {
    if (cfg->format == FMT_MKV || cfg_has_rtmp(cfg)) {
      log_line(TOOL_NAME ": -f mkv/-o rtmp:// can't hold multiple programs, pick one with -p <pid>");
      mpts_probe_print_programs(TOOL_NAME, &probe);
      return 1;
    }
    for (int k = 0; k < probe.program_count; k++)
      all_pids[(*n_all_pids)++] = probe.programs[k].pmt_pid;
    return 0;
  }
  for (int k = 0; k < probe.program_count; k++)
    if (probe.programs[k].pmt_pid == cfg->pmt_pid) {
      *pmt_pid = cfg->pmt_pid;
      return 0;
    }
  log_line(TOOL_NAME ": -p 0x%04x not found in this MPTS", cfg->pmt_pid);
  mpts_probe_print_programs(TOOL_NAME, &probe);
  return 1;
}
