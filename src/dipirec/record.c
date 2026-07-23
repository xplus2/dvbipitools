/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "filter/ts.h"
#include "lib/demux/psi.h"
#include "lib/demux/rtp.h"
#include "lib/demux/tspack.h"
#include "lib/log.h"
#include "lib/net/multicast.h"
#include "lib/net/udpxy.h"
#include "lib/signal.h"
#include "mux/mkv.h"
#include "record.h"
#include "ret_client.h"
#include "version.h"

typedef struct {
  uri_kind_t kind;
  mcast_t *m;
  udpxy_t *u;
  ret_client_t *ret; /* NULL unless --ret */
} src_t;

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int src_open(const config_t *cfg, src_t *s) {
  memset(s, 0, sizeof *s);
  s->kind = cfg->source.kind;
  if (s->kind == URI_UDPXY) {
    s->u = udpxy_open(cfg->source.http_host, cfg->source.http_port, cfg->source.http_path, TOOL_NAME "/" TOOL_VERSION);
    return s->u ? 0 : -1;
  }
  s->m = mcast_open(cfg->source.family, cfg->source.group, cfg->source.port, cfg->iface, 1000);
  if (!s->m)
    return -1;
  if (cfg->ret.enabled) {
    s->ret = ret_client_open(cfg);
    if (!s->ret) {
      mcast_close(s->m);
      return -1;
    }
  }
  return 0;
}

/* TS bytes, RTP stripped. >0 len, 0 timeout, -1 end */
static ssize_t src_read(src_t *s, unsigned char *buf, size_t cap) {
  ssize_t n;
  size_t off;

  if (s->kind == URI_UDPXY)
    return udpxy_read(s->u, buf, cap);
  if (s->ret)
    return ret_client_read(s->ret, s->m, buf, cap);
  n = mcast_recv(s->m, buf, cap);
  if (n <= 0)
    return n;
  off = rtp_payload_offset(buf, (size_t)n);
  if (off) {
    memmove(buf, buf + off, (size_t)n - off);
    n -= (ssize_t)off;
  }
  return n;
}

static void src_close(src_t *s) {
  if (s->ret)
    ret_client_close(s->ret);
  if (s->m)
    mcast_close(s->m);
  if (s->u)
    udpxy_close(s->u);
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
  return cfg->duration_s && mono() - start >= (double)cfg->duration_s;
}

static int psi_cb(void *v, const unsigned char *pkt) {
  psi_feed((psi_t *)v, pkt);
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
static int run_raw(src_t *s, const config_t *cfg, int out, unsigned long long *bytes, double start) {
  unsigned char buf[65536];
  tspack_t pz = {{0}, 0};
  psi_t *psi = cfg->verbose ? psi_new() : NULL;
  double last_stat = 0;
  int rc = 0;

  while (!stop_now(cfg, start)) {
    ssize_t n = src_read(s, buf, sizeof buf);
    if (n < 0)
      break;
    if (n == 0)
      continue;
    if (write_all(out, buf, (size_t)n)) {
      rc = 1;
      break;
    }
    *bytes += (unsigned long long)n;
    if (psi)
      tspack_feed(&pz, buf, (size_t)n, psi_cb, psi);
    if (cfg->verbose && mono() - last_stat >= 1.0) {
      stats_show(cfg, mono() - start, *bytes, psi);
      last_stat = mono();
    }
  }
  psi_free(psi);
  return rc;
}

typedef struct {
  ts_filter_t *f;
  int out;
  unsigned long long *bytes;
  int bad;
} ts_ctx_t;

static int ts_cb(void *v, const unsigned char *pkt) {
  ts_ctx_t *t = v;
  unsigned char o[188];

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
static int run_ts(src_t *s, const config_t *cfg, int out, unsigned long long *bytes, double start) {
  unsigned char buf[65536];
  tspack_t pz = {{0}, 0};
  ts_filter_t *f = ts_filter_new(cfg->audio_all, cfg->audio_track, cfg->subs == SUB_STRIP);
  ts_ctx_t tc;
  double last_stat = 0;
  int rc = 0;

  if (!f)
    return 1;
  tc.f = f;
  tc.out = out;
  tc.bytes = bytes;
  tc.bad = 0;

  while (!stop_now(cfg, start)) {
    ssize_t n = src_read(s, buf, sizeof buf);
    if (n < 0)
      break;
    if (n == 0)
      continue;
    if (tspack_feed(&pz, buf, (size_t)n, ts_cb, &tc)) {
      if (tc.bad)
        log_line_ansi("audio track \e[0;31m%u\e[0;33m not found (\e[0;33m%d\e[0;33m present)", cfg->audio_track, psi_audio_count(ts_filter_psi(f)));
      rc = 1;
      break;
    }
    if (cfg->verbose && mono() - last_stat >= 1.0) {
      stats_show(cfg, mono() - start, *bytes, ts_filter_psi(f));
      last_stat = mono();
    }
  }
  ts_filter_free(f);
  return rc;
}

static int mkv_pkt_cb(void *v, const unsigned char *pkt) {
  mkv_feed((mkv_t *)v, pkt);
  return mkv_error((mkv_t *)v);
}

/* mkv/mka: packetize, demux PES, mux */
static int run_mkv(src_t *s, const config_t *cfg, int out, unsigned long long *bytes, double start, int video_ok) {
  unsigned char buf[65536];
  tspack_t pz;
  mkv_t *m;
  double last_stat = 0;
  int rc = 0;

  memset(&pz, 0, sizeof pz);
  m = mkv_new(out, cfg, video_ok, bytes);
  if (!m)
    return 1;

  while (!stop_now(cfg, start)) {
    ssize_t n = src_read(s, buf, sizeof buf);
    if (n < 0)
      break;
    if (n == 0)
      continue;
    if (tspack_feed(&pz, buf, (size_t)n, mkv_pkt_cb, m)) {
      rc = 1;
      break;
    }
    if (cfg->verbose && mono() - last_stat >= 1.0) {
      stats_show(cfg, mono() - start, *bytes, mkv_psi(m));
      last_stat = mono();
    }
  }
  mkv_close(m);
  return rc;
}

int record_run(const config_t *cfg) {
  unsigned long long bytes = 0;
  double start;
  src_t s;
  int out, rc;

  out = open_output(cfg->out_path);
  if (out < 0)
    return 1;
  if (src_open(cfg, &s)) {
    if (out != STDOUT_FILENO)
      close(out);
    return 1;
  }

  start = mono();
  if (cfg->format == FMT_MKV || cfg->format == FMT_MKA)
    rc = run_mkv(&s, cfg, out, &bytes, start, cfg->format == FMT_MKV);
  else if (cfg->format == FMT_TS)
    rc = run_ts(&s, cfg, out, &bytes, start);
  else
    rc = run_raw(&s, cfg, out, &bytes, start);

  src_close(&s); /* IGMP/MLD leave */
  if (out != STDOUT_FILENO)
    close(out);

  if (cfg->verbose && log_stderr_is_tty())
    fputc('\n', stderr); /* off stats line */
  if (rc == 0) {
    log_line_ansi("recorded for \e[0;33m%.1f\e[0ms, \e[0;33m%.1f\e[0mMB written", mono() - start, (double)bytes / 1048576.0);
    log_line("done.");
  }
  return rc;
}
