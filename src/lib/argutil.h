/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_ARGUTIL_H
#define LIB_ARGUTIL_H

#include <stdarg.h>
#include <stddef.h>

/* tool prefixed stderr line: "<tool>: <fmt>\n" */
void argutil_verr(const char *tool, const char *fmt, va_list ap) __attribute__((format(printf, 2, 0)));

/* port 1..65535, digits only */
int argutil_port_parse(const char *p, unsigned *out);

typedef struct {
  const char *name;
  int value;
} enum_map_t;

int map_lookup(const enum_map_t *m, size_t n, const char *s, int *out);

/* [addr]:<port> or <addr4>:<port>. no multicast/unicast restriction, caller's own job */
int argutil_addrport_parse(const char *s, int *family, char *addr_out, size_t addr_out_sz, unsigned *port_out);

#endif
