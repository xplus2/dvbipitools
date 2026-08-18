/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>
#include <time.h>

#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/signal.h"
#include "mpts_probe.h"
#include "tspack.h"

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

typedef struct {
  psi_t *psi;
} probe_cb_ctx_t;

static int probe_cb(void *v, const unsigned char *pkt) {
  psi_feed(((probe_cb_ctx_t *)v)->psi, pkt);
  return 0;
}

static int all_named(const psi_t *psi, int *count) {
  const psi_multi_program_t *m;
  int i, n;

  m = psi_multi_programs(psi, &n);
  if (count)
    *count = n;
  if (!n)
    return 0;
  for (i = 0; i < n; i++)
    if (!m[i].resolved || !m[i].service_name[0])
      return 0;
  return 1;
}

static void fill_result(const psi_t *psi, mpts_probe_result_t *r) {
  const psi_multi_program_t *m;
  int i, n;

  m = psi_multi_programs(psi, &n);
  r->program_count = (n > PSI_MAX_PROGRAMS) ? PSI_MAX_PROGRAMS : n;
  for (i = 0; i < r->program_count; i++) {
    r->programs[i].program_number = m[i].program_number;
    r->programs[i].pmt_pid = m[i].pmt_pid;
    bufcpy(r->programs[i].name, sizeof r->programs[i].name, m[i].service_name);
  }
  r->kind = (r->program_count > 1) ? MPTS_PROBE_MPTS : MPTS_PROBE_SPTS;
}

mpts_probe_result_t mpts_probe_run(tssrc_t *src, int name_wait_ms) {
  mpts_probe_result_t r;
  unsigned char buf[65536];
  tspack_t pz;
  probe_cb_ctx_t ctx;
  double name_deadline = 0; /* gcc maybe-uninitialized fp: set with have_deadline flag */
  int have_deadline = 0;

  memset(&r, 0, sizeof r);
  memset(&pz, 0, sizeof pz);
  ctx.psi = psi_new();
  if (!ctx.psi) {
    r.kind = MPTS_PROBE_FAIL;
    return r;
  }
  psi_enable_multi_program(ctx.psi);

  for (;;) {
    ssize_t n = tssrc_read(src, buf, sizeof buf, NULL);
    if (n < 0 || signal_stop_requested()) {
      r.kind = MPTS_PROBE_FAIL;
      psi_free(ctx.psi);
      return r;
    }
    if (n > 0)
      tspack_feed(&pz, buf, (size_t)n, probe_cb, &ctx);

    if (!psi_have_pat(ctx.psi))
      continue;

    {
      int count;
      int named = all_named(ctx.psi, &count);
      if (count <= 1)
        break; /* spts: name not needed */
      if (!have_deadline) {
        have_deadline = 1;
        name_deadline = mono() + (double)name_wait_ms / 1000.0;
      }
      if (named || mono() >= name_deadline)
        break;
    }
  }

  fill_result(ctx.psi, &r);
  psi_free(ctx.psi);
  return r;
}

void mpts_probe_print_programs(const char *tool_name, const mpts_probe_result_t *r) {
  int i;

  log_line("%s: %d program(s) available:", tool_name, r->program_count);
  for (i = 0; i < r->program_count; i++)
    log_line("%s:   sid=%u pmt_pid=0x%04x %s", tool_name, r->programs[i].program_number, r->programs[i].pmt_pid, r->programs[i].name[0] ? r->programs[i].name : "(no SDT)");
}
