/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "filter/pace.h"
#include "filter/ts.h"
#include "lib/demux/mpts_probe.h"
#include "lib/demux/psi/psi.h"
#include "lib/demux/tspack.h"
#include "lib/log.h"
#include "lib/net/ristout.h"
#include "lib/net/rtmpout.h"
#include "lib/net/tssink.h"
#include "lib/net/tssource.h"
#include "lib/mux/flv/flv.h"
#include "lib/mux/mkv/mkv.h"
#include "lib/signal.h"
#include "record.h"
#include "ret_client.h"
#include "version.h"

#define MPTS_NAME_WAIT_MS 3000

typedef struct {
  uri_kind_t kind;
  tssrc_t *t;
  ret_client_t *ret; /* NULL unless --ret */
} src_t;

static int src_open(const config_t *cfg, src_t *s) {
  tssrc_cfg_t tc;

  memset(s, 0, sizeof *s);
  s->kind = cfg->source.kind;

  memset(&tc, 0, sizeof tc);
  tc.user_agent = TOOL_NAME "/" TOOL_VERSION;
  if (s->kind == URI_HTTP) {
    tc.kind = TSSRC_HTTP;
    tc.http = cfg->source.http;
    tc.insecure_tls = cfg->insecure_tls;
  } else if (s->kind == URI_FILE) {
    if (cfg->source.file_path[0]) {
      tc.kind = TSSRC_FILE;
      tc.file_path = cfg->source.file_path;
    } else {
      tc.kind = TSSRC_STDIN;
    }
  } else {
    tc.kind = (s->kind == URI_RTP) ? TSSRC_RTP : TSSRC_UDP;
    tc.family = cfg->source.family;
    tc.group = cfg->source.group;
    tc.port = cfg->source.port;
    tc.iface = cfg->iface_in;
  }

  s->t = tssrc_open(&tc, NULL);
  if (!s->t)
    return -1;

  if (s->kind != URI_HTTP && cfg->ret.enabled) {
    s->ret = ret_client_open(cfg);
    if (!s->ret) {
      tssrc_close(s->t);
      return -1;
    }
  }
  return 0;
}

/* TS bytes, RTP stripped. >0 len, 0 timeout, -1 end */
static ssize_t src_read(src_t *s, unsigned char *buf, size_t cap) {
  if (s->ret)
    return ret_client_read(s->ret, tssrc_mcast(s->t), buf, cap);
  return tssrc_read(s->t, buf, cap, NULL);
}

static void src_close(src_t *s) {
  if (s->ret)
    ret_client_close(s->ret);
  tssrc_close(s->t);
}

static int open_output(const char *path) {
  int fd;

  if (strcmp(path, "-") == 0)
    return STDOUT_FILENO;
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    log_line("open %s: %s", path, strerror(errno));
  return fd;
}

static int write_all(int fd, const unsigned char *p, size_t n) {
  while (n) {
    ssize_t w = write(fd, p, n);
    if (w < 0) {
      if (errno == EINTR)
        continue;
      log_line("w:%s", strerror(errno));
      return -1;
    }
    p += w;
    n -= (size_t)w;
  }
  return 0;
}

/* file/stdout via a plain fd, rtp/udp via tssink, rist:// via ristout.
   fd valid: run_mkv needs it directly, always file/stdout case there (args rejects mkv/mka with net -o) */
typedef struct {
  int fd;
  tssink_t *net;   /* NULL unless -o rtp:// or udp:// */
  ristout_t *rist; /* NULL unless -o rist:// */
  int net_had_error;  /* edge-log gate, net send failure never stops recording */
  int rist_had_error; /* edge-log gate, rist write failure never stops recording */
  uint64_t errors_total; /* metrics: cumulative write failures, net/rist only */
} out_sink_t;

static int sink_open(const config_t *cfg, const out_target_t *t, out_sink_t *o) {
  o->net = NULL;
  o->rist = NULL;
  o->net_had_error = 0;
  o->rist_had_error = 0;
  o->errors_total = 0;
  if (t->kind == OUT_FILE) {
    o->fd = open_output(t->file_path);
    return o->fd < 0 ? -1 : 0;
  }
  o->fd = -1;
  if (t->kind == OUT_RIST) {
    ristout_cfg_t rc;
    memset(&rc, 0, sizeof rc);
    rc.peer_uri[0] = t->rist_uri;
    rc.npeers = 1;
    rc.profile = cfg->rist_profile == RIST_PROF_MAIN ? RISTOUT_PROFILE_MAIN : RISTOUT_PROFILE_SIMPLE;
    rc.secret = cfg->rist_secret;
    rc.cname = cfg->rist_cname;
    rc.buffer_ms = cfg->rist_buffer_ms;
    rc.verbose = cfg->verbose;
    o->rist = ristout_open(&rc);
    return o->rist ? 0 : -1;
  }
  {
    tssink_cfg_t tc;
    memset(&tc, 0, sizeof tc);
    tc.kind = (t->kind == OUT_RTP) ? TSSINK_RTP : TSSINK_UDP;
    tc.family = t->family;
    tc.group = t->group;
    tc.port = t->port;
    tc.iface = cfg->iface_out;
    tc.ttl = cfg->out_ttl;
    o->net = tssink_open(&tc);
  }
  return o->net ? 0 : -1;
}

/* failed network sink never fatal, log only on failure/recovery edge, retry every write */
static void note_send_result(int ok, int *had_error, uint64_t *errors_total, const char *label) {
  if (!ok) {
    (*errors_total)++;
    if (!*had_error) {
      log_line("%s output: write failed, will keep retrying", label);
      *had_error = 1;
    }
  } else if (*had_error) {
    log_line("%s output: recovered", label);
    *had_error = 0;
  }
}

static int sink_write(out_sink_t *o, const unsigned char *p, size_t n) {
  if (o->rist) {
    note_send_result(ristout_write(o->rist, p, n) >= 0, &o->rist_had_error, &o->errors_total, "rist");
    return 0;
  }
  if (o->net) {
    note_send_result(tssink_write(o->net, p, n) >= 0, &o->net_had_error, &o->errors_total, "net");
    return 0;
  }
  return write_all(o->fd, p, n);
}

static void sink_close(out_sink_t *o) {
  if (o->rist) {
    ristout_close(o->rist);
    return;
  }
  if (o->net) {
    tssink_close(o->net);
    return;
  }
  if (o->fd >= 0 && o->fd != STDOUT_FILENO)
    close(o->fd);
}

/* N rtmpout_t, independently paced: one target down doesn't block others */
typedef struct {
  rtmpout_t *out[DIPIREC_MAX_OUT];
  int had_error[DIPIREC_MAX_OUT];
  uint64_t errors_total[DIPIREC_MAX_OUT]; /* metrics: cumulative write failures per target */
  int n;
} rtmp_fanout_t;

static int rtmp_fanout_open(const config_t *cfg, rtmp_fanout_t *r) {
  r->n = 0;
  for (int i = 0; i < cfg->n_out; i++) {
    rtmpout_cfg_t rc;
    if (cfg->out[i].kind != OUT_RTMP && cfg->out[i].kind != OUT_RTMPS)
      continue;
    memset(&rc, 0, sizeof rc);
    rc.url = cfg->out[i].rtmp_url;
    rc.insecure = cfg->insecure_tls;
    r->out[r->n] = rtmpout_open(&rc);
    if (!r->out[r->n])
      return -1;
    r->had_error[r->n] = 0;
    r->errors_total[r->n] = 0;
    r->n++;
  }
  return 0;
}

static void rtmp_fanout_cb(void *ctx, flv_tag_type_t type, uint32_t timestamp_ms, const unsigned char *data, size_t len) {
  rtmp_fanout_t *r = ctx;
  static const char *const labels[DIPIREC_MAX_OUT] = {
    "rtmp[0]", "rtmp[1]", "rtmp[2]", "rtmp[3]",
    "rtmp[4]", "rtmp[5]", "rtmp[6]", "rtmp[7]"
  };

  for (int i = 0; i < r->n; i++) {
    note_send_result(rtmpout_write(r->out[i], type, timestamp_ms, data, len) >= 0, &r->had_error[i], &r->errors_total[i], labels[i]);
  }
}

static void rtmp_fanout_close(rtmp_fanout_t *r) {
  for (int i = 0; i < r->n; i++)
    rtmpout_close(r->out[i]);
}

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

/* clamp: keeps formatted length bounded */
void fmt_dur(double secs, char *buf, size_t n) {
  long s, h, m;
  s = (secs > 0.0) ? (long)secs : 0;
  if (s < 0)
    s = 0;
  if (s > 359999) /* cap 99:59:59 */
    s = 359999;
  h = s / 3600;
  m = (s % 3600) / 60;
  if (h)
    snprintf(buf, n, "%2ld:%02ld:%02ld", h, m, s % 60);
  else
    snprintf(buf, n, "%2ld:%02ld", m, s % 60);
}

/* one same-line stats update, tty only */
static void stats_show(const config_t *cfg, double elapsed, unsigned long long bytes, const psi_t *psi) {
  char line[200], dur[16];
  const char *name = "?";
  const char *subs = "-";
  int atr = 0, len;

  if (!log_stderr_is_tty())
    return;
  if (psi) {
    int c, tt = 0, sb = 0;
    const psi_es_t *es = psi_es(psi, &c);
    if (*psi_service_name(psi))
      name = psi_service_name(psi);
    atr = psi_audio_count(psi);
    for (int k = 0; k < c; k++) {
      if (es[k].cls == PID_TELETEXT)
        tt = 1;
      if (es[k].cls == PID_SUBTITLE)
        sb = 1;
    }
    subs = (tt && sb) ? "ttx+sub" : tt ? "txt" : sb ? "sub" : "-";
  }
  fmt_dur(elapsed, dur, sizeof dur);
  len = snprintf(line, sizeof line, "%s %.1fMB %s a=%d s=%s", dur, (double)bytes / 1048576.0, name, atr, subs);
  if (cfg->duration_s && len > 0 && len < (int)sizeof line) {
    double pct = elapsed * 100.0 / (double)cfg->duration_s;
    time_t st = time(NULL) + (time_t)((double)cfg->duration_s - elapsed);
    struct tm tm;
    char stop[16];
    if (pct > 100.0)
      pct = 100.0;
    gmtime_r(&st, &tm);
    strftime(stop, sizeof stop, "%H:%M:%S", &tm);
    snprintf(line + len, sizeof line - (size_t)len, "%.1f%% stop=%s", pct, stop);
  }
  fprintf(stderr, "\r%s\033[K", line);
  fflush(stderr);
}

/* label o<i>: net/rist sinks only, up/retry concept. label rtmp<i>: rtmp targets */
static void push_metrics(metrics_exporter_t *mx, const config_t *cfg, out_sink_t *sinks, int n_sinks,
                         const rtmp_fanout_t *rf, unsigned long long bytes, double start) {
  metrics_writer_t w;
  char label[16];

  if (!metrics_exporter_due(mx, mono_seconds()) || metrics_exporter_begin(mx, &w, TOOL_VERSION))
    return;
  metrics_writer_put(&w, METRICS_ID_REC_BYTES_TOTAL, NULL, bytes);
  metrics_writer_put(&w, METRICS_ID_REC_ELAPSED_SECONDS, NULL, (uint64_t)(mono_seconds() - start));
  metrics_writer_put(&w, METRICS_ID_REC_DURATION_LIMIT_SECONDS, NULL, cfg->duration_s > 0 ? (uint64_t)cfg->duration_s : 0);
  for (int i = 0; i < n_sinks; i++) {
    if (!sinks[i].net && !sinks[i].rist)
      continue;
    snprintf(label, sizeof label, "o%d", i);
    metrics_writer_put(&w, METRICS_ID_REC_OUTPUT_UP, label, (sinks[i].net_had_error || sinks[i].rist_had_error) ? 0 : 1);
    metrics_writer_put(&w, METRICS_ID_REC_OUTPUT_ERRORS_TOTAL, label, sinks[i].errors_total);
  }
  for (int i = 0; i < rf->n; i++) {
    snprintf(label, sizeof label, "rtmp%d", i);
    metrics_writer_put(&w, METRICS_ID_REC_OUTPUT_UP, label, rf->had_error[i] ? 0 : 1);
    metrics_writer_put(&w, METRICS_ID_REC_OUTPUT_ERRORS_TOTAL, label, rf->errors_total[i]);
  }
  metrics_exporter_send(mx, &w);
}

/* no flv/rtmp path here, args.c already rejects -f raw + an rtmp(s) target */
static int run_raw(src_t *s, const config_t *cfg, out_sink_t *sinks, int n_sinks, const rtmp_fanout_t *rf,
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
    const psi_t *p = c->f ? ts_filter_psi(c->f) : c->m ? mkv_psi(c->m) : flv_psi(c->flv);
    pace_feed_pcr_pkt(c->pace, pkt, psi_pcr_pid(p));
  }
  if (c->f) {
    unsigned char o[188];
    if (ts_filter_packet(c->f, pkt, o)) {
      if (write_to_sinks(c->sinks, c->n_sinks, o, 188))
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
static int run_stream(src_t *s, const config_t *cfg, out_sink_t *sinks, int n_sinks, int mkv_fd, rtmp_fanout_t *rf,
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
    char app_name[64], srcuri[1024];
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
      const psi_t *p = ctx.f ? ts_filter_psi(ctx.f) : ctx.m ? mkv_psi(ctx.m) : flv_psi(ctx.flv);
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
static int resolve_pmt_selection(const config_t *cfg, src_t *s, unsigned *pmt_pid, unsigned *all_pids, int *n_all_pids) {
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
