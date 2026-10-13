/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_MPTS_PROBE_H
#define DVBIPITOOLS_LIB_DEMUX_MPTS_PROBE_H

#include "lib/net/ts/source.h"
#include "psi/psi.h"

typedef struct {
  unsigned program_number;
  unsigned pmt_pid;
  char name[PSI_NAME]; /* "" if not resolved within name-wait budget */
} mpts_probe_program_t;

typedef enum {
  MPTS_PROBE_SPTS, /* programs[0] valid, program_count == 1 */
  MPTS_PROBE_MPTS, /* program_count > 1 */
  MPTS_PROBE_FAIL  /* source died/EOF/interrupted/timeout before any PAT arrived */
} mpts_probe_kind_t;

typedef struct {
  mpts_probe_kind_t kind;
  mpts_probe_program_t programs[PSI_MAX_PROGRAMS];
  int program_count;
} mpts_probe_result_t;

#define MPTS_PROBE_PAT_WAIT_MS 10000

mpts_probe_result_t mpts_probe_run(tssrc_t *src, int pat_wait_ms, int name_wait_ms);

/* "N programs available:" + one line per program, via log_line() */
void mpts_probe_print_programs(const char *tool_name, const mpts_probe_result_t *r);

#endif
