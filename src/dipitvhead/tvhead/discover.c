/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/log.h"
#include "lib/signal.h"
#include "priv.h"

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

void print_discovered(const psi_t *psi) {
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

/* one non-blocking increment of discovery. 1 ready, 0 still pending, -1 hard error or -p pid
   not in PAT. caller tracks its own timeout and calls psi_select_pmt_pid() up front if wanted. */
int discover_step(discover_state_t *ds, tvsrc_t *src, const dipitvhead_input_t *input, psi_t *psi, input_metrics_t *im) {
  unsigned char buf[65536];
  net_err_reason_t reason = NET_ERR_OTHER;
  ssize_t n = tvsrc_read(src, buf, sizeof buf, &reason);

  input_metrics_note_read(im, n, reason);
  if (n < 0)
    return -1;
  if (n > 0)
    tspack_feed(&ds->pz, buf, (size_t)n, psi_cb, psi);

  if (psi_have_pat(psi) && !ds->listed) {
    print_program_list(psi);
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
      log_line("-p 0x%x not present in PAT", input->pmt_pid);
      return -1;
    }
    ds->checked_pmt_pid = 1;
  }
  if (psi_ready(psi))
    return 1;
  return 0;
}

/* 1 ready, 0 timeout, -1 hard error or -p pid not in PAT */
int discover(tvsrc_t *src, const dipitvhead_input_t *input, psi_t *psi, input_metrics_t *im) {
  discover_state_t ds;
  double start = mono_seconds();

  memset(&ds, 0, sizeof ds);
  if (input->pmt_pid)
    psi_select_pmt_pid(psi, input->pmt_pid);

  while (!signal_stop_requested()) {
    int r = discover_step(&ds, src, input, psi, im);
    if (r)
      return r;
    if (mono_seconds() - start >= DISCOVERY_TIMEOUT_S)
      return 0;
  }
  return -1;
}
