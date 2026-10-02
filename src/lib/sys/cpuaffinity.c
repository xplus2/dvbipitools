/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#include "cpuaffinity.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <sched.h>

#include "lib/helper/log.h"
#endif

static int parse_cpu(const char **p, unsigned *out) {
  char *end;
  unsigned long v;
  if (**p < '0' || **p > '9') return -1;
  errno = 0;
  v = strtoul(*p, &end, 10);
  if (errno || v >= CPUAFF_MAX) return -1;
  *p = end;
  *out = (unsigned)v;
  return 0;
}

int cpuaff_parse(cpuaff_t *out, const char *s) {
  cpuaff_t tmp;
  unsigned char seen[CPUAFF_MAX];
  const char *p = s;

  if (!strcmp(s, "off")) {
    memset(out, 0, sizeof *out);
    return 0;
  }
  if (!strcmp(s, "auto")) {
    memset(out, 0, sizeof *out);
    out->mode = CPUAFF_AUTO;
    return 0;
  }
  memset(&tmp, 0, sizeof tmp);
  memset(seen, 0, sizeof seen);
  tmp.mode = CPUAFF_LIST;
  for (;;) {
    unsigned lo;
    unsigned hi;
    if (parse_cpu(&p, &lo)) return -1;
    hi = lo;
    if (*p == '-') {
      p++;
      if (parse_cpu(&p, &hi) || hi < lo) return -1;
    }
    for (; lo <= hi; lo++) {
      if (seen[lo]) return -1;
      seen[lo] = 1;
      tmp.cpus[tmp.n++] = (unsigned short)lo;
    }
    if (*p == '\0') break;
    if (*p++ != ',') return -1;
  }
  *out = tmp;
  return 0;
}

#ifdef __linux__
int cpuaff_pin(const cpuaff_t *a, unsigned idx, const char *what) {
  cpu_set_t allowed;
  cpu_set_t one;
  unsigned cpu = 0;

  if (!a || a->mode == CPUAFF_OFF) return 0;
  if (sched_getaffinity(0, sizeof allowed, &allowed)) {
    log_line("cpu-affinity: %s: sched_getaffinity: %s", what, strerror(errno));
    return -1;
  }
  if (a->mode == CPUAFF_LIST) {
    if (idx >= a->n) return 1;
    cpu = a->cpus[idx];
    if (!CPU_ISSET(cpu, &allowed)) {
      log_line("cpu-affinity: %s: cpu %u not in allowed set", what, cpu);
      return -1;
    }
  } else {
    int count = CPU_COUNT(&allowed);
    unsigned want;
    if (count < 1) return -1;
    want = idx % (unsigned)count;
    for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
      if (!CPU_ISSET(cpu, &allowed)) continue;
      if (want == 0) break;
      want--;
    }
  }
  CPU_ZERO(&one);
  CPU_SET(cpu, &one);
  if (sched_setaffinity(0, sizeof one, &one)) {
    log_line("cpu-affinity: %s: cpu %u: %s", what, cpu, strerror(errno));
    return -1;
  }
  return 0;
}
#else
int cpuaff_pin(const cpuaff_t *a, unsigned idx, const char *what) {
  (void)a;
  (void)idx;
  (void)what;
  return 0;
}
#endif
