/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "lib/log.h"
#include "lib/signal.h"
#include "priv.h"

int tvhead_run_single(const config_t *cfg, metrics_exporter_t *mx) {
  int rc = 0;
  mcast_t *outmc;
  out_ctx_t out;
  input_metrics_t im;
  ts_metrics_t tsm;
  int metrics_on = metrics_exporter_enabled(mx);
  input_metrics_t *im_p = metrics_on ? &im : NULL;
  ts_metrics_t *tsm_p = metrics_on ? &tsm : NULL;

  memset(&out, 0, sizeof out);
  memset(&im, 0, sizeof im);
  memset(&tsm, 0, sizeof tsm);
  outmc = mcast_open_send(cfg->family, cfg->mcast_group, cfg->mcast_port, cfg->iface_out, (int)cfg->ttl);
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
  if (cfg->n_rist > 0) {
    out.rist = tvhead_rist_open(cfg);
    if (!out.rist) {
      if (out.rtph)
        rtpheader_free(out.rtph);
      mcast_close(outmc);
      return 1;
    }
  }

  while (!signal_stop_requested()) {
    net_err_reason_t reason = NET_ERR_OTHER;
    tvsrc_t *src = tvsrc_open(cfg, &cfg->inputs[0], &reason);
    psi_t *psi;
    int r;

    if (!src) {
      if (metrics_on) {
        im.up = 0;
        im.errors_total[reason]++;
      }
      if (cfg->error_retry_s <= 0) {
        rc = 1;
        break;
      }
      log_line("input error, retrying in %lds", cfg->error_retry_s);
      sleep_interruptible(cfg->error_retry_s);
      continue;
    }
    if (metrics_on) {
      if (im.seen_open)
        im.reconnects_total++;
      im.seen_open = 1;
      im.up = 1;
    }

    psi = psi_new();
    if (!psi) {
      tvsrc_close(src);
      rc = 1;
      break;
    }

    r = discover(src, &cfg->inputs[0], psi, im_p);
    if (r == 1) {
      out_program_pids_t pids;
      remux_t *rx;

      out_program_pids(0, &pids);
      rx = remux_new(cfg, &cfg->inputs[0], psi, &pids, 1);
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
          int cas_wanted = cfg->cas_algo != CAS_ALGO_NONE || cfg->biss2_enabled || cfg->biss1_enabled || cfg->biss2_ca_enabled;
          cas_t *cas = NULL;
          if (cas_wanted) {
            int es_count;
            const out_es_t *es = remux_es(rx, &es_count);
            cas = cas_start(cfg, psi, es, es_count, remux_pcr_pid_out(rx));
            if (cas)
              remux_set_cas(rx, cas);
            else
              log_line("cas setup failed");
          }
          if (cas_wanted && !cas) {
            r = -1;
          } else {
            print_discovered(psi);
            run_output(src, rx, &out, cfg, cas, mx, im_p, tsm_p);
            if (cas)
              cas_stop(cas);
          }
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
    if (metrics_on)
      im.up = 0;
    if (signal_stop_requested())
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
  if (out.rist)
    ristout_close(out.rist);
  mcast_close(outmc);

  if (cfg->verbose && log_stderr_is_tty())
    fputc('\n', stderr);
  if (rc == 0)
    log_line("stopped.");
  return rc ? 1 : 0;
}
