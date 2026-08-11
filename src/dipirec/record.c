/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
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
#include "lib/net/tssource.h"
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
  if (s->kind == URI_UDPXY) {
    tc.kind = TSSRC_UDPXY;
    tc.udpxy_host = cfg->source.http_host;
    tc.udpxy_port = cfg->source.http_port;
    tc.udpxy_path = cfg->source.http_path;
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
    tc.iface = cfg->iface;
  }

  s->t = tssrc_open(&tc, NULL);
  if (!s->t)
    return -1;

  if (s->kind != URI_UDPXY && cfg->ret.enabled) {
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

static int stop_now(const config_t *cfg, double start) {
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
static void fmt_dur(double secs, char *buf, size_t n) {
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
    int c, k, tt = 0, sb = 0;
    const psi_es_t *es = psi_es(psi, &c);
    if (*psi_service_name(psi))
      name = psi_service_name(psi);
    atr = psi_audio_count(psi);
    for (k = 0; k < c; k++) {
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

/* raw: write TS */
static int run_raw(src_t *s, const config_t *cfg, int out, unsigned long long *bytes, double start, pace_ctrl_t *pace) {
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
    if (write_all(out, buf, (size_t)n)) {
      rc = 1;
      break;
    }
    *bytes += (unsigned long long)n;
    if (cfg->verbose && mono_seconds() - last_stat >= 1.0) {
      stats_show(cfg, mono_seconds() - start, *bytes, ctx.psi);
      last_stat = mono_seconds();
    }
  }
  psi_free(ctx.psi);
  return rc;
}

typedef struct {
  ts_filter_t *f;
  int out;
  unsigned long long *bytes;
  int bad;
  pace_ctrl_t *pace; /* NULL unless --pace */
  int pace_pcr;       /* 1: this chunk wasn't RTP-framed, pace per packet */
} ts_ctx_t;

static int ts_cb(void *v, const unsigned char *pkt) {
  ts_ctx_t *t = v;
  unsigned char o[188];

  if (t->pace_pcr)
    pace_feed_pcr_pkt(t->pace, pkt, psi_pcr_pid(ts_filter_psi(t->f)));
  if (ts_filter_packet(t->f, pkt, o)) {
    if (write_all(t->out, o, 188))
      return 1;
    *t->bytes += 188;
  }
  if (ts_filter_bad_track(t->f)) {
    t->bad = 1;
    return 1;
  }
  return 0;
}

/* ts: packetize, filter, write */
static int run_ts(src_t *s, const config_t *cfg, int out, unsigned long long *bytes, double start, unsigned pmt_pid,
                   pace_ctrl_t *pace) {
  unsigned char buf[65536];
  tspack_t pz = {{0}, 0};
  ts_filter_t *f = ts_filter_new(cfg->audio_all, cfg->audio_track, cfg->subs == SUB_STRIP, pmt_pid, cfg->strip_mask);
  ts_ctx_t tc;
  double last_stat = 0;
  int rc = 0;

  if (!f)
    return 1;
  tc.f = f;
  tc.out = out;
  tc.bytes = bytes;
  tc.bad = 0;
  tc.pace = pace;
  tc.pace_pcr = 0;

  while (!stop_now(cfg, start)) {
    ssize_t n = src_read(s, buf, sizeof buf);
    if (n < 0)
      break;
    if (n == 0)
      continue;
    if (pace) {
      if (tssrc_is_rtp_framed(s->t)) {
        pace_feed_rtp_ts(pace, tssrc_last_rtp_ts(s->t));
        tc.pace_pcr = 0;
      } else {
        tc.pace_pcr = 1;
      }
    }
    if (tspack_feed(&pz, buf, (size_t)n, ts_cb, &tc)) {
      if (tc.bad)
        log_line_ansi("audio track \e[0;31m%u\e[0;33m not found (\e[0;33m%d\e[0;33m present)", cfg->audio_track, psi_audio_count(ts_filter_psi(f)));
      rc = 1;
      break;
    }
    if (cfg->verbose && mono_seconds() - last_stat >= 1.0) {
      stats_show(cfg, mono_seconds() - start, *bytes, ts_filter_psi(f));
      last_stat = mono_seconds();
    }
  }
  ts_filter_free(f);
  return rc;
}

typedef struct {
  mkv_t *m;
  pace_ctrl_t *pace; /* NULL unless --pace */
  int pace_pcr;       /* 1: this chunk wasn't RTP-framed, pace per packet */
} mkv_ctx_t;

static int mkv_pkt_cb(void *v, const unsigned char *pkt) {
  mkv_ctx_t *c = v;
  if (c->pace_pcr)
    pace_feed_pcr_pkt(c->pace, pkt, psi_pcr_pid(mkv_psi(c->m)));
  mkv_feed(c->m, pkt);
  return mkv_error(c->m);
}

/* mkv/mka: packetize, demux PES, mux. pmt_pid: 0 or -p <pid>. all_pids/n_all_pids: -p all */
static int run_mkv(src_t *s, const config_t *cfg, int out, unsigned long long *bytes, double start, int video_ok,
                    unsigned pmt_pid, const unsigned *all_pids, int n_all_pids, pace_ctrl_t *pace) {
  unsigned char buf[65536];
  char app_name[64], srcuri[1024];
  tspack_t pz;
  mkv_opts_t opts;
  mkv_ctx_t ctx;
  mkv_t *m;
  double last_stat = 0;
  int rc = 0;

  memset(&pz, 0, sizeof pz);
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
    m = mkv_new(out, &opts, video_ok, bytes, all_pids, n_all_pids);
  else if (pmt_pid)
    m = mkv_new(out, &opts, video_ok, bytes, &pmt_pid, 1);
  else
    m = mkv_new(out, &opts, video_ok, bytes, NULL, 0);
  if (!m)
    return 1;
  ctx.m = m;
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
    if (tspack_feed(&pz, buf, (size_t)n, mkv_pkt_cb, &ctx)) {
      rc = 1;
      break;
    }
    if (cfg->verbose && mono_seconds() - last_stat >= 1.0) {
      stats_show(cfg, mono_seconds() - start, *bytes, mkv_psi(m));
      last_stat = mono_seconds();
    }
  }
  mkv_close(m);
  return rc;
}

/* mpts discovery + -p decision. 0: proceed (pmt_pid/all_pids/n_all_pids filled in).
   1: abort, message already printed. raw skips this - nothing to select there. */
static int resolve_pmt_selection(const config_t *cfg, src_t *s, unsigned *pmt_pid, unsigned *all_pids, int *n_all_pids) {
  mpts_probe_result_t probe;
  int k;

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
    if (cfg->format == FMT_MKV) {
      log_line(TOOL_NAME ": -f mkv can't hold multiple programs, pick one with -p <pid>");
      mpts_probe_print_programs(TOOL_NAME, &probe);
      return 1;
    }
    for (k = 0; k < probe.program_count; k++)
      all_pids[(*n_all_pids)++] = probe.programs[k].pmt_pid;
    return 0;
  }
  for (k = 0; k < probe.program_count; k++)
    if (probe.programs[k].pmt_pid == cfg->pmt_pid) {
      *pmt_pid = cfg->pmt_pid;
      return 0;
    }
  log_line(TOOL_NAME ": -p 0x%04x not found in this MPTS", cfg->pmt_pid);
  mpts_probe_print_programs(TOOL_NAME, &probe);
  return 1;
}

int record_run(const config_t *cfg) {
  unsigned long long bytes = 0;
  double start;
  src_t s;
  int out, rc;
  unsigned pmt_pid, all_pids[PSI_MAX_PROGRAMS];
  int n_all_pids;
  pace_ctrl_t *pace;

  out = open_output(cfg->out_path);
  if (out < 0)
    return 1;
  if (src_open(cfg, &s)) {
    if (out != STDOUT_FILENO)
      close(out);
    return 1;
  }

  if (resolve_pmt_selection(cfg, &s, &pmt_pid, all_pids, &n_all_pids)) {
    src_close(&s);
    if (out != STDOUT_FILENO)
      close(out);
    return 1;
  }
  if (cfg->source.kind == URI_FILE) /* probing above consumed bytes it can't give back */
    tssrc_rewind(s.t);

  pace = cfg->pace ? pace_new() : NULL;
  start = mono_seconds();
  if (cfg->format == FMT_MKV || cfg->format == FMT_MKA)
    rc = run_mkv(&s, cfg, out, &bytes, start, cfg->format == FMT_MKV, pmt_pid, all_pids, n_all_pids, pace);
  else if (cfg->format == FMT_TS)
    /* -p all: nothing left to filter */
    rc = (n_all_pids > 0) ? run_raw(&s, cfg, out, &bytes, start, pace) : run_ts(&s, cfg, out, &bytes, start, pmt_pid, pace);
  else
    rc = run_raw(&s, cfg, out, &bytes, start, pace);
  pace_free(pace);

  src_close(&s); /* IGMP/MLD leave */
  if (out != STDOUT_FILENO)
    close(out);

  if (cfg->verbose && log_stderr_is_tty())
    fputc('\n', stderr); /* off stats line */
  if (rc == 0) {
    log_line_ansi("recorded for \e[0;33m%.1f\e[0ms, \e[0;33m%.1f\e[0mMB written", mono_seconds() - start, (double)bytes / 1048576.0);
    log_line("done.");
  }
  return rc;
}
