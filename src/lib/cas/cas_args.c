/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "cas_args.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "lib/argutil.h"

int cas_super_id_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 0);
  if (*end != '\0' || v == 0 || v > 0xFFFFFFFFUL)
    return -1;
  *out = (unsigned)v;
  return 0;
}

int cas_endpoint_parse(const char *s, char *host_out, size_t host_out_sz, unsigned *port_out) {
  const char *p = s, *host, *colon;
  size_t hostlen;
  char *end;
  unsigned long port;

  if (!strncmp(p, "tcp://", 6))
    p += 6;
  if (*p == '[') {
    const char *close = strchr(p, ']');
    if (!close)
      return -1;
    host = p + 1;
    hostlen = (size_t)(close - host);
    if (close[1] != ':')
      return -1;
    colon = close + 1;
  } else {
    host = p;
    colon = strrchr(p, ':');
    if (!colon)
      return -1;
    hostlen = (size_t)(colon - host);
  }
  if (hostlen == 0 || hostlen >= host_out_sz)
    return -1;
  memcpy(host_out, host, hostlen);
  host_out[hostlen] = '\0';

  port = strtoul(colon + 1, &end, 10);
  if (end == colon + 1 || port == 0 || port > 65535)
    return -1;
  if (*end != '\0' && *end != '/')
    return -1;
  *port_out = (unsigned)port;
  return 0;
}

int cas_version_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 10);
  if (*end != '\0' || (v != 2 && v != 3))
    return -1;
  *out = (unsigned)v;
  return 0;
}

static void args_err(const char *tool, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void args_err(const char *tool, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  argutil_verr(tool, fmt, ap);
  va_end(ap);
}

int cas_args_validate(const char *tool_name, cas_algo_t cas_algo, const cas_vendor_t *vendors, unsigned n_vendors,
                       int biss2_enabled, int biss1_enabled, int biss2_ca_enabled, int biss2_emit_esw,
                       int biss2_ca_session_id_given, unsigned cas_cp_duration_ms) {
  if (biss2_enabled && (cas_algo != CAS_ALGO_NONE || n_vendors > 0)) {
    args_err(tool_name, "--biss2-sw is mutually exclusive with --cas-algo/--cas-ecmg");
    return -1;
  }
  if (biss1_enabled && (cas_algo != CAS_ALGO_NONE || n_vendors > 0)) {
    args_err(tool_name, "--biss1-sw is mutually exclusive with --cas-algo/--cas-ecmg");
    return -1;
  }
  if (biss2_ca_enabled && (cas_algo != CAS_ALGO_NONE || n_vendors > 0)) {
    args_err(tool_name, "--biss2-ca-receivers is mutually exclusive with --cas-algo/--cas-ecmg");
    return -1;
  }
  if (biss2_enabled && biss1_enabled) {
    args_err(tool_name, "--biss2-sw and --biss1-sw are mutually exclusive");
    return -1;
  }
  if (biss2_ca_enabled && (biss2_enabled || biss1_enabled)) {
    args_err(tool_name, "--biss2-ca-receivers is mutually exclusive with --biss2-sw/--biss1-sw");
    return -1;
  }
  if (biss2_emit_esw && !biss2_enabled) {
    args_err(tool_name, "--biss2-emit-esw requires --biss2-sw");
    return -1;
  }
  if (biss2_ca_session_id_given && !biss2_ca_enabled) {
    args_err(tool_name, "--biss2-ca-session-id requires --biss2-ca-receivers");
    return -1;
  }
  if (biss2_ca_enabled && cas_cp_duration_ms < 1000) {
    args_err(tool_name, "--biss2-ca-receivers needs --cas-cp-duration >= 1000 (Tech 3292-s1 T_ECM_change_min)");
    return -1;
  }
  if (cas_algo == CAS_ALGO_NONE)
    return 0;
  if (n_vendors == 0) {
    args_err(tool_name, "--cas-algo requires --cas-ecmg");
    return -1;
  }
  for (unsigned vi = 0; vi < n_vendors; vi++) {
    const cas_vendor_t *v = &vendors[vi];
    if (!v->super_cas_id) {
      args_err(tool_name, "--cas-ecmg %s:%u requires --cas-super-id", v->ecmg_host, v->ecmg_port);
      return -1;
    }
    if (!v->ecm_id) {
      args_err(tool_name, "--cas-ecmg %s:%u requires --cas-ecm-id", v->ecmg_host, v->ecmg_port);
      return -1;
    }
    if (v->ecm_pid == v->emm_pid) {
      args_err(tool_name, "--cas-ecm-pid and --cas-emm-pid must differ (--cas-ecmg %s:%u)", v->ecmg_host, v->ecmg_port);
      return -1;
    }
    for (unsigned vj = vi + 1; vj < n_vendors; vj++) {
      const cas_vendor_t *o = &vendors[vj];
      if (v->ecm_pid == o->ecm_pid || v->ecm_pid == o->emm_pid || v->emm_pid == o->ecm_pid || v->emm_pid == o->emm_pid) {
        args_err(tool_name, "--cas-ecm-pid/--cas-emm-pid collide across --cas-ecmg vendors");
        return -1;
      }
      if (v->emmg_port == o->emmg_port) {
        args_err(tool_name, "--cas-emmg-port %u used by more than one --cas-ecmg vendor (each needs its own EMMG listener)", v->emmg_port);
        return -1;
      }
    }
  }
  return 0;
}
