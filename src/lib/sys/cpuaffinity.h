/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_SYS_CPUAFFINITY_H
#define DVBIPITOOLS_LIB_SYS_CPUAFFINITY_H

#define CPUAFF_MAX 1024

typedef enum { CPUAFF_OFF = 0, CPUAFF_AUTO, CPUAFF_LIST } cpuaff_mode_t;

/* zero-initialized = off */
typedef struct {
  cpuaff_mode_t mode;
  unsigned n; /* CPUAFF_LIST: entries in cpus[] */
  unsigned short cpus[CPUAFF_MAX];
} cpuaff_t;

/* "off", "auto", list like "2-4,6,8" (no overlaps or duplicates). 0 ok, -1 invalid, *out untouched */
int cpuaff_parse(cpuaff_t *out, const char *s);

/* pin calling thread. auto: idx-th allowed CPU, wraps. list: cpus[idx].
   0 pinned or off, 1 no entry for idx (thread floats), -1 failed (what= thread label) */
int cpuaff_pin(const cpuaff_t *a, unsigned idx, const char *what);

#endif
