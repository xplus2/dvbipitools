/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_ARGUTIL_H
#define LIB_ARGUTIL_H

#include <stdarg.h>
#include <stddef.h>

/* tool prefixed stderr line: "<tool>: <fmt>\n" */
void argutil_verr(const char *tool, const char *fmt, va_list ap) __attribute__((format(printf, 2, 0)));
void argutil_err(const char *tool, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* port 1..65535, digits only */
int argutil_port_parse(const char *p, unsigned *out);

/* dec only, [min, max] incl. 0 ok, -1 bad format or oor */
int argutil_uint_range(const char *s, unsigned min, unsigned max, unsigned *out);

int argutil_bufcpy_opt(const char *tool, char *dst, size_t dstsz, const char *val, const char *optname);

int argutil_srt_passphrase_opt(const char *tool, const char *passphrase, const char *optname);

int argutil_metrics_interval_opt(const char *tool, const char *val, unsigned *out);
int argutil_metrics_opts_validate(const char *tool, const char *sock, const char *id, unsigned interval_s);

typedef struct {
  const char *name;
  int value;
} enum_map_t;

int map_lookup(const enum_map_t *m, size_t n, const char *s, int *out);

/* [addr]:<port> or <addr4>:<port>. no multicast/unicast restriction, caller's own job */
int argutil_addrport_parse(const char *s, int *family, char *addr_out, size_t addr_out_sz, unsigned *port_out);

#endif
