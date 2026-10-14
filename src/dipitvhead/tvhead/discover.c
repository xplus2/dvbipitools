/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/helper/log.h"
#include "lib/sys/signal.h"
#include "priv.h"

typedef struct {
  psi_t *psi;
  tsinspect_t *insp;
} discover_feed_t;

static int psi_cb(void *v, const unsigned char *pkt) {
  psi_feed((psi_t *)v, pkt);
  return 0;
}

static int psi_cb_inspect(void *v, const unsigned char *pkt) {
  discover_feed_t *df = v;
  tsinspect_packet(df->insp, pkt);
  psi_feed(df->psi, pkt);
  return 0;
}

static void print_program_list(const psi_t *psi, unsigned idx) {
  int n;
  const psi_program_t *p = psi_pat_programs(psi, &n);
  log_line_ansi("input \e[1;30m%u\e[0m: PAT: %d program(s) found", idx, n);
  for (int i = 0; i < n; i++) log_line_ansi("  program \e[1;33m%u\e[0m, PMT \e[0;36m0x%x\e[0m", p[i].program_number, p[i].pmt_pid);
}

void print_discovered(const psi_t *psi, unsigned idx) {
  int n, i;
  const psi_es_t *es = psi_es(psi, &n);
  log_line_ansi("input \e[1;30m%u\e[0m: \e[1;32mlocked\e[0m: program \e[0;33m%u\e[0m (PMT \e[0;36m0x%x\e[0m, PCR \e[0;36m0x%x\e[0m)", idx, psi_program_number(psi), psi_pmt_pid(psi), psi_pcr_pid(psi));
  for (i = 0; i < n; i++) {
    const psi_es_t *e = &es[i];
    if (e->lang[0]) log_line_ansi("  pid \e[0;36m0x%x\e[0m: %s (%s) lang=\e[1;30m%s\e[0m", e->pid, pid_class_name(e->cls), codec_name(e->codec), e->lang);
    else            log_line_ansi("  pid \e[0;36m0x%x\e[0m: %s (%s)", e->pid, pid_class_name(e->cls), codec_name(e->codec));
  }
  if (*psi_service_name(psi)) log_line_ansi("  SDT: service=\"\e[0;33m%s\e[0m\" provider=\"\e[0;33m%s\e[0m\"", psi_service_name(psi), psi_provider_name(psi));
  if (*psi_network_name(psi)) log_line_ansi("  NIT: network=\"\e[0;33m%s\e[0m\"", psi_network_name(psi));
}

/* one non-blocking increment of discovery. PMT w/o ES stays pending. 1 ready, 0 still pending, -1 hard error or -p pid
   not in PAT. caller tracks its own timeout and calls psi_select_pmt_pid() up front if wanted. */
int discover_step(discover_state_t *ds, tvsrc_t *src, const dipitvhead_input_t *input, psi_t *psi, input_metrics_t *im) {
  unsigned char buf[65536];
  net_err_reason_t reason = NET_ERR_OTHER;
  ssize_t n = tvsrc_read(src, buf, sizeof buf, &reason);

  input_metrics_note_read(im, n, reason);
  if (n < 0) return -1;
  if (n > 0 && ds->insp) {
    discover_feed_t df = {psi, ds->insp};
    tsinspect_tick(ds->insp, mono_seconds());
    tsinspect_set_rx_ns(ds->insp, tvsrc_last_rx_ns(src));
    tsinspect_set_buffer_ms(ds->insp, tvsrc_buffer_ms(src));
    tspack_feed_sync(&ds->pz, buf, (size_t)n, psi_cb_inspect, &df, tsinspect_sync(ds->insp));
  } else if (n > 0) tspack_feed(&ds->pz, buf, (size_t)n, psi_cb, psi);
  if (psi_have_pat(psi) && !ds->listed) {
    print_program_list(psi, ds->idx);
    ds->listed = 1;
  }
  if (input->pmt_pid && ds->listed && !ds->checked_pmt_pid) {
    int cnt, k, found = 0;
    const psi_program_t *p = psi_pat_programs(psi, &cnt);
    for (k = 0; k < cnt; k++)
      if (p[k].pmt_pid == input->pmt_pid) {
        found = 1;
        break;
      }
    if (!found) {
      log_code("TE000611", "-p 0x%x not present in PAT", input->pmt_pid);
      return -1;
    }
    ds->checked_pmt_pid = 1;
  }
  if (psi_ready(psi)) {
    int es_n;
    psi_es(psi, &es_n);
    if (es_n > 0) return 1;
  }
  return 0;
}

#define DISCOVER_WAIT_MS 100

/* 1 ready, 0 timeout, -1 hard error or -p pid not in PAT */
int discover_insp(tvsrc_t *src, const dipitvhead_input_t *input, psi_t *psi, input_metrics_t *im, tsinspect_t *insp) {
  discover_state_t ds;
  double start = mono_seconds();
  memset(&ds, 0, sizeof ds);
  ds.insp = insp;
  if (input->pmt_pid)
    psi_select_pmt_pid(psi, input->pmt_pid);

  while (!signal_stop_requested()) {
    int r;
    double left = DISCOVERY_TIMEOUT_S - (mono_seconds() - start);
    if (left <= 0) return 0;
    if (tvsrc_wait(src, left < DISCOVER_WAIT_MS / 1000.0 ? (int)(left * 1000.0) + 1 : DISCOVER_WAIT_MS) < 0) return -1;
    r = discover_step(&ds, src, input, psi, im);
    if (r) return r;
  }
  return -1;
}

int discover(tvsrc_t *src, const dipitvhead_input_t *input, psi_t *psi, input_metrics_t *im) {
  return discover_insp(src, input, psi, im, NULL);
}
