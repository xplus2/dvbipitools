/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lib/demux/psi.h"
#include "lib/demux/tspack.h"
#include "lib/log.h"
#include "lib/mux/rtpheader.h"
#include "lib/net/multicast.h"
#include "lib/signal.h"

#include "input/source.h"
#include "mux/bitrate.h"
#include "mux/remux.h"
#include "tvhead.h"
#include "version.h"

/* how long to watch PAT-listed PMT candidates before giving up */
#define DISCOVERY_TIMEOUT_S 8.0
#define TS_PER_DGRAM 7

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void sleep_interruptible(long seconds) {
  time_t deadline = time(NULL) + seconds;
  while (!signal_stop_requested() && time(NULL) < deadline)
    usleep(200000);
}

static int psi_cb(void *v, const unsigned char *pkt) {
  psi_feed((psi_t *)v, pkt);
  return 0;
}

static void print_program_list(const psi_t *psi) {
  int n, i;
  const psi_program_t *p = psi_pat_programs(psi, &n);
  log_line("PAT: %d program(s) found", n);
  for (i = 0; i < n; i++)
    log_line("  program %u, PMT pid 0x%x", p[i].program_number, p[i].pmt_pid);
}

static void print_discovered(const psi_t *psi) {
  int n, i;
  const psi_es_t *es = psi_es(psi, &n);
  log_line_ansi("locked: program \e[0;33m%u\e[0m (PMT pid 0x%x, PCR pid 0x%x)", psi_program_number(psi), psi_pmt_pid(psi), psi_pcr_pid(psi));
  for (i = 0; i < n; i++) {
    const psi_es_t *e = &es[i];
    if (e->lang[0])
      log_line("  pid 0x%x: %s (%s) lang=%s", e->pid, pid_class_name(e->cls), codec_name(e->codec), e->lang);
    else
      log_line("  pid 0x%x: %s (%s)", e->pid, pid_class_name(e->cls), codec_name(e->codec));
  }
  if (*psi_service_name(psi))
    log_line("  SDT: service=\"%s\" provider=\"%s\"", psi_service_name(psi), psi_provider_name(psi));
  if (*psi_network_name(psi))
    log_line("  NIT: network=\"%s\"", psi_network_name(psi));
}

/* 1 ready, 0 timeout, -1 hard error or -p pid not in PAT */
static int discover(tvsrc_t *src, const config_t *cfg, psi_t *psi) {
  unsigned char buf[65536];
  tspack_t pz;
  double start = mono();
  int listed = 0, checked_pmt_pid = 0;

  memset(&pz, 0, sizeof pz);
  if (cfg->pmt_pid)
    psi_select_pmt_pid(psi, cfg->pmt_pid);

  while (!signal_stop_requested()) {
    ssize_t n = tvsrc_read(src, buf, sizeof buf);
    if (n < 0)
      return -1;
    if (n > 0)
      tspack_feed(&pz, buf, (size_t)n, psi_cb, psi);

    if (psi_have_pat(psi) && !listed) {
      print_program_list(psi);
      listed = 1;
    }
    if (cfg->pmt_pid && listed && !checked_pmt_pid) {
      int cnt, k, found = 0;
      const psi_program_t *p = psi_pat_programs(psi, &cnt);
      for (k = 0; k < cnt; k++)
        if (p[k].pmt_pid == cfg->pmt_pid) {
          found = 1;
          break;
        }
      if (!found) {
        log_line("-p 0x%x not present in PAT", cfg->pmt_pid);
        return -1;
      }
      checked_pmt_pid = 1;
    }
    if (psi_ready(psi))
      return 1;
    if (mono() - start >= DISCOVERY_TIMEOUT_S)
      return 0;
  }
  return -1;
}

typedef struct {
  mcast_t *mc;
  int rtp;
  rtpheader_t *rtph;
  bitrate_pacer_t *pacer;
  unsigned char batch[TS_PER_DGRAM * 188];
  int batch_count;
  int had_error;
  unsigned long long packets;
} out_ctx_t;

static void flush_batch(out_ctx_t *o) {
  unsigned char dgram[12 + TS_PER_DGRAM * 188];
  size_t off = 0;

  if (o->batch_count == 0)
    return;
  if (o->rtp)
    off = rtpheader_build(o->rtph, (uint32_t)(mono() * 90000.0), dgram, sizeof dgram);
  memcpy(dgram + off, o->batch, (size_t)o->batch_count * 188);
  if (mcast_send(o->mc, dgram, off + (size_t)o->batch_count * 188) < 0)
    o->had_error = 1;
  o->batch_count = 0;
}

static void packet_cb(void *ctx, const unsigned char *pkt188) {
  out_ctx_t *o = ctx;
  bitrate_pace(o->pacer);
  memcpy(o->batch + (size_t)o->batch_count * 188, pkt188, 188);
  o->batch_count++;
  o->packets++;
  bitrate_account(o->pacer);
  if (o->batch_count == TS_PER_DGRAM)
    flush_batch(o);
}

static void send_null_packet(out_ctx_t *o) {
  unsigned char pkt[188];
  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = 0x1F;
  pkt[2] = 0xFF;
  pkt[3] = 0x10;
  packet_cb(o, pkt);
}

typedef struct {
  remux_t *rx;
  out_ctx_t *out;
} feed_ctx_t;

static int remux_cb(void *v, const unsigned char *pkt) {
  feed_ctx_t *f = v;
  remux_feed(f->rx, pkt, packet_cb, f->out);
  return 0;
}

/* steady-state: read, remux, send, until stop/hard error. returns 0 clean stop, -1 error */
static int run_output(tvsrc_t *src, remux_t *rx, out_ctx_t *out, const config_t *cfg) {
  unsigned char buf[65536];
  tspack_t pz;
  feed_ctx_t fc;
  double start = mono(), last_stat = 0;

  memset(&pz, 0, sizeof pz);
  fc.rx = rx;
  fc.out = out;

  while (!signal_stop_requested()) {
    int stuff_n, k;
    ssize_t n = tvsrc_read(src, buf, sizeof buf);
    if (n < 0)
      return -1;
    if (n > 0)
      tspack_feed(&pz, buf, (size_t)n, remux_cb, &fc);
    if (out->had_error)
      return -1;
    stuff_n = bitrate_stuff_due(out->pacer);
    for (k = 0; k < stuff_n; k++)
      send_null_packet(out);
    if (out->had_error)
      return -1;
    if (cfg->verbose && mono() - last_stat >= 1.0) {
      fprintf(stderr, "\r%.0fs, %llu TS packets\033[K", mono() - start, out->packets);
      fflush(stderr);
      last_stat = mono();
    }
  }
  return 0;
}

int tvhead_run(const config_t *cfg) {
  int rc = 0;
  mcast_t *outmc;
  out_ctx_t out;

  memset(&out, 0, sizeof out);
  outmc = mcast_open_send(cfg->family, cfg->mcast_group, cfg->mcast_port, cfg->iface, (int)cfg->ttl);
  if (!outmc)
    return 1;
  out.mc = outmc;
  out.rtp = cfg->rtp;
  if (cfg->rtp) {
    out.rtph = rtpheader_new();
    if (!out.rtph) {
      mcast_close(outmc);
      return 1;
    }
  }

  while (!signal_stop_requested()) {
    tvsrc_t *src = tvsrc_open(cfg);
    psi_t *psi;
    int r;

    if (!src) {
      if (cfg->error_retry_s <= 0) {
        rc = 1;
        break;
      }
      log_line("input error, retrying in %lds", cfg->error_retry_s);
      sleep_interruptible(cfg->error_retry_s);
      continue;
    }

    psi = psi_new();
    if (!psi) {
      tvsrc_close(src);
      rc = 1;
      break;
    }

    r = discover(src, cfg, psi);
    if (r == 1) {
      remux_t *rx = remux_new(cfg, psi);
      if (!rx) {
        log_line("remux setup failed");
        r = -1;
      } else {
        out.pacer = bitrate_pacer_new(cfg->bitrate_kbps ? (double)cfg->bitrate_kbps * 1000.0 : 0.0, cfg->stuff, cfg->burst_limit);
        if (!out.pacer) {
          log_line("bitrate pacer setup failed");
          remux_free(rx);
          r = -1;
        } else {
          print_discovered(psi);
          run_output(src, rx, &out, cfg);
          bitrate_pacer_free(out.pacer);
          out.pacer = NULL;
          remux_free(rx);
        }
      }
    } else if (r == 0) {
      log_line("no live PMT found within %.0fs (use -p to select one, or check the source)", DISCOVERY_TIMEOUT_S);
    }
    psi_free(psi);
    tvsrc_close(src);
    if (signal_stop_requested() || out.had_error)
      break;
    if (cfg->error_retry_s <= 0) {
      rc = 1;
      break;
    }
    log_line("retrying in %lds", cfg->error_retry_s);
    sleep_interruptible(cfg->error_retry_s);
  }

  flush_batch(&out);
  if (out.rtph)
    rtpheader_free(out.rtph);
  mcast_close(outmc);

  if (cfg->verbose && log_stderr_is_tty())
    fputc('\n', stderr);
  if (rc == 0 && !out.had_error)
    log_line("stopped.");
  return (rc || out.had_error) ? 1 : 0;
}
