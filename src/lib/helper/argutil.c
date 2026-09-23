/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "argutil.h"
#include "ioutil.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void argutil_verr(const char *tool, const char *fmt, va_list ap) {
  fputs(tool, stderr);
  fputs(": ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
}

void argutil_err(const char *tool, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  argutil_verr(tool, fmt, ap);
  va_end(ap);
}

int argutil_port_parse(const char *p, unsigned *out) {
  char *end;
  unsigned long v;
  if (*p == '\0') return -1;
  errno = 0;
  v = strtoul(p, &end, 10);
  if (errno || *end != '\0' || v == 0 || v > 65535) return -1;
  *out = (unsigned)v;
  return 0;
}

int argutil_bufcpy_opt(const char *tool, char *dst, size_t dstsz, const char *val, const char *optname) {
  if (bufcpy(dst, dstsz, val) >= dstsz) {
    argutil_err(tool, "%s too long", optname);
    return -1;
  }
  return 0;
}

int argutil_srt_passphrase_opt(const char *tool, const char *passphrase, const char *optname) {
  size_t len = strlen(passphrase);
  if (passphrase[0] && (len < 10 || len > 79)) {
    argutil_err(tool, "%s must be 10..79 characters", optname);
    return -1;
  }
  return 0;
}

int argutil_metrics_interval_opt(const char *tool, const char *val, unsigned *out) {
  if (argutil_uint_range(val, 1, 86400, out)) {
    argutil_err(tool, "invalid --metrics-interval: %s (seconds, 1..86400)", val);
    return -1;
  }
  return 0;
}

int argutil_metrics_opts_validate(const char *tool, const char *sock, const char *id, unsigned interval_s) {
  if ((sock || interval_s) && !id) {
    argutil_err(tool, "--metrics/--metrics-interval require --metrics-id");
    return -1;
  }
  return 0;
}

int metrics_known_pids_parse(const char *s, unsigned *out, unsigned *n) {
  unsigned cnt = 0;
  while (*s) {
    char *end;
    unsigned long v;
    errno = 0;
    v = strtoul(s, &end, 0);
    if (end == s || errno || v > 8191 || cnt == METRICS_KNOWN_PIDS_MAX) return -1;
    out[cnt++] = (unsigned)v;
    if (*end == ',') end++;
    else if (*end) return -1;
    s = end;
  }
  *n = cnt;
  return cnt ? 0 : -1;
}

int argutil_metrics_known_pids_opt(const char *tool, const char *val, unsigned *out, unsigned *n) {
  if (metrics_known_pids_parse(val, out, n)) {
    argutil_err(tool, "invalid --metrics-inspect-ts-pids: %s (comma separated pids, 0..8191)", val);
    return -1;
  }
  return 0;
}

int metrics_inspect_ts_parse(const char *s, metrics_inspect_ts_t *out) {
  static const enum_map_t levels[] = {
    {"off", METRICS_INSPECT_TS_OFF},
    {"basic", METRICS_INSPECT_TS_BASIC},
    {"medium", METRICS_INSPECT_TS_MEDIUM},
    {"full", METRICS_INSPECT_TS_FULL},
  };
  int v;
  if (map_lookup(levels, sizeof levels / sizeof *levels, s, &v)) return -1;
  *out = (metrics_inspect_ts_t)v;
  return 0;
}

int argutil_metrics_inspect_ts_opt(const char *tool, const char *val, metrics_inspect_ts_t *out) {
  if (metrics_inspect_ts_parse(val, out)) {
    argutil_err(tool, "invalid --metrics-inspect-ts: %s (off|basic|medium|full)", val);
    return -1;
  }
  return 0;
}

int argutil_metrics_inspect_ts_validate(const char *tool, const char *id, metrics_inspect_ts_t level) {
  if (level != METRICS_INSPECT_TS_OFF && !id) {
    argutil_err(tool, "--metrics-inspect-ts requires --metrics-id");
    return -1;
  }
  return 0;
}

int argutil_uint_range(const char *s, unsigned min, unsigned max, unsigned *out) {
  char *end;
  unsigned long v;
  if (*s == '\0') return -1;
  errno = 0;
  v = strtoul(s, &end, 10);
  if (errno || *end != '\0' || v < min || v > max) return -1;
  *out = (unsigned)v;
  return 0;
}

int map_lookup(const enum_map_t *m, size_t n, const char *s, int *out) {
  for (size_t i = 0; i < n; i++) if (strcmp(s, m[i].name) == 0) {
    *out = m[i].value;
    return 0;
  }
  return -1;
}

int argutil_addrport_parse(const char *s, int *family, char *addr_out, size_t addr_out_sz, unsigned *port_out) {
  char addr[64];
  if (*s == '[') {
    const char *close = strchr(s, ']');
    size_t len;
    if (!close) return -1;
    len = (size_t)(close - (s + 1));
    if (len == 0 || len >= sizeof addr) return -1;
    memcpy(addr, s + 1, len);
    addr[len] = '\0';
    if (close[1] != ':' || argutil_port_parse(close + 2, port_out)) return -1;
    *family = AF_INET6;
  } else {
    const char *colon = strrchr(s, ':');
    size_t len;
    if (!colon) return -1;
    len = (size_t)(colon - s);
    if (len == 0 || len >= sizeof addr) return -1;
    memcpy(addr, s, len);
    addr[len] = '\0';
    if (argutil_port_parse(colon + 1, port_out)) return -1;
    *family = AF_INET;
  }
  if (*family == AF_INET) {
    struct in_addr a;
    if (inet_pton(AF_INET, addr, &a) != 1) return -1;
  } else {
    struct in6_addr a6;
    if (inet_pton(AF_INET6, addr, &a6) != 1) return -1;
  }

  {
    size_t alen = strlen(addr);
    if (alen >= addr_out_sz) return -1;
    memcpy(addr_out, addr, alen + 1);
  }
  return 0;
}
