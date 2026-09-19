/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "cas_args.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/biss/biss.h"
#include "lib/cas/ecmg_client/cw_encryption.h"
#include "lib/cas/emmg_server/emmg_server.h"
#include "lib/helper/argutil.h"
#include "lib/scrambler/scrambler.h"

int cas_super_id_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 0);
  if (*end != '\0' || v == 0 || v > 0xFFFFFFFFUL) return -1;
  *out = (unsigned)v;
  return 0;
}

int cas_endpoint_parse(const char *s, char *host_out, size_t host_out_sz, unsigned *port_out) {
  const char *p = s, *host, *colon;
  size_t hostlen;
  char *end;
  unsigned long port;

  if (!strncmp(p, "tcp://", 6)) p += 6;
  if (*p == '[') {
    const char *close = strchr(p, ']');
    if (!close) return -1;
    host = p + 1;
    hostlen = (size_t)(close - host);
    if (close[1] != ':') return -1;
    colon = close + 1;
  } else {
    host = p;
    colon = strrchr(p, ':');
    if (!colon) return -1;
    hostlen = (size_t)(colon - host);
  }
  if (hostlen == 0 || hostlen >= host_out_sz) return -1;
  memcpy(host_out, host, hostlen);
  host_out[hostlen] = '\0';

  port = strtoul(colon + 1, &end, 10);
  if (end == colon + 1 || port == 0 || port > 65535) return -1;
  if (*end != '\0' && *end != '/') return -1;
  *port_out = (unsigned)port;
  return 0;
}

int cas_version_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 10);
  if (*end != '\0' || (v != 2 && v != 3)) return -1;
  *out = (unsigned)v;
  return 0;
}

static int bad(char *err, size_t errsz, const char *val, const char *want) {
  snprintf(err, errsz, "invalid '%s' (%s)", val, want);
  return -1;
}

static int set_text(char *dst, size_t sz, const char *val, char *err, size_t errsz) {
  if (strlen(val) >= sz) {
    snprintf(err, errsz, "too long (max %zu)", sz - 1);
    return -1;
  }
  memcpy(dst, val, strlen(val) + 1);
  return 0;
}

static int id_parse(const char *s, unsigned *out) {
  return argutil_uint_range(s, 1, 0xFFFF, out);
}

static int pid_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 0);
  if (*end != '\0' || v == 0 || v > 0x1FFE) return -1;
  *out = (unsigned)v;
  return 0;
}

void cas_vendor_defaults(cas_vendor_t *v) {
  memset(v, 0, sizeof *v);
  v->ecm_pid = 0x0020;
  v->emmg_port = 8002;
  v->emm_pid = 0x0021;
}

int cas_vendor_add(cas_vendor_t *vendors, unsigned *n_vendors, const char *endpoint, char *err, size_t errsz) {
  cas_vendor_t *v;
  if (*n_vendors >= ARGS_MAX_CAS_VENDORS) {
    snprintf(err, errsz, "too many vendors (max %d)", ARGS_MAX_CAS_VENDORS);
    return -1;
  }
  v = &vendors[*n_vendors];
  cas_vendor_defaults(v);
  if (endpoint && cas_vendor_set_endpoint(v, endpoint, err, errsz)) return -1;
  (*n_vendors)++;
  return 0;
}

int cas_vendor_set_endpoint(cas_vendor_t *v, const char *val, char *err, size_t errsz) {
  if (cas_endpoint_parse(val, v->ecmg_host, sizeof v->ecmg_host, &v->ecmg_port)) return bad(err, errsz, val, "tcp://host:port");
  return 0;
}

int cas_vendor_set_ecmg_version(cas_vendor_t *v, const char *val, char *err, size_t errsz) {
  return cas_version_parse(val, &v->ecmg_version) ? bad(err, errsz, val, "2|3") : 0;
}

int cas_vendor_set_super_id(cas_vendor_t *v, const char *val, char *err, size_t errsz) {
  return cas_super_id_parse(val, &v->super_cas_id) ? bad(err, errsz, val, "32 bit, dec or 0x-hex") : 0;
}

int cas_vendor_set_ecm_id(cas_vendor_t *v, const char *val, char *err, size_t errsz) {
  return id_parse(val, &v->ecm_id) ? bad(err, errsz, val, "1..65535") : 0;
}

int cas_vendor_set_ecm_pid(cas_vendor_t *v, const char *val, char *err, size_t errsz) {
  return pid_parse(val, &v->ecm_pid) ? bad(err, errsz, val, "0x0001..0x1FFE") : 0;
}

int cas_vendor_set_emmg_port(cas_vendor_t *v, const char *val, char *err, size_t errsz) {
  if (argutil_port_parse(val, &v->emmg_port)) return bad(err, errsz, val, "port");
  v->emmg_port_given = 1;
  return 0;
}

int cas_vendor_set_emmg_reverse(cas_vendor_t *v, const char *val, char *err, size_t errsz) {
  if (cas_endpoint_parse(val, v->emmg_reverse_host, sizeof v->emmg_reverse_host, &v->emmg_reverse_port)) return bad(err, errsz, val, "tcp://host:port");
  return 0;
}

int cas_vendor_set_emmg_version(cas_vendor_t *v, const char *val, char *err, size_t errsz) {
  return cas_version_parse(val, &v->emmg_version) ? bad(err, errsz, val, "2|3") : 0;
}

int cas_vendor_set_emmg_max_conns(cas_vendor_t *v, const char *val, char *err, size_t errsz) {
  if (argutil_uint_range(val, 1, EMMG_MAX_CONNS_CEILING, &v->emmg_max_conns)) {
    snprintf(err, errsz, "invalid '%s' (1..%u)", val, (unsigned)EMMG_MAX_CONNS_CEILING);
    return -1;
  }
  return 0;
}

int cas_vendor_set_emm_pid(cas_vendor_t *v, const char *val, char *err, size_t errsz) {
  return pid_parse(val, &v->emm_pid) ? bad(err, errsz, val, "0x0001..0x1FFE") : 0;
}

int cas_vendor_set_resilience(cas_vendor_t *v, const char *val, char *err, size_t errsz) {
  static const enum_map_t map[] = {{"frozen", CAS_OUTAGE_FROZEN}, {"cycling", CAS_OUTAGE_CYCLING}, {"silent", CAS_OUTAGE_SILENT}};
  int m;
  if (map_lookup(map, sizeof map / sizeof map[0], val, &m)) return bad(err, errsz, val, "frozen|cycling|silent");
  v->resilience = (cas_outage_mode_t)m;
  return 0;
}

int cas_vendor_set_cwenc_algo(cas_vendor_t *v, const char *val, char *err, size_t errsz) {
  if (strcmp(val, "des56") && strcmp(val, "aes128") && strcmp(val, "aes256")) return bad(err, errsz, val, "des56|aes128|aes256");
  return set_text(v->cwenc_algorithm, sizeof v->cwenc_algorithm, val, err, errsz);
}

int cas_vendor_set_cwenc_aes_mode(cas_vendor_t *v, const char *val, char *err, size_t errsz) {
  if (strcmp(val, "stream") && strcmp(val, "ecb")) return bad(err, errsz, val, "stream|ecb");
  return set_text(v->cwenc_aes_mode, sizeof v->cwenc_aes_mode, val, err, errsz);
}

int cas_vendor_set_cwenc_fixed_key(cas_vendor_t *v, const char *val, char *err, size_t errsz) {
  return set_text(v->cwenc_fixed_key_hex, sizeof v->cwenc_fixed_key_hex, val, err, errsz);
}

int cas_vendor_set_cwenc_key_list_a(cas_vendor_t *v, const char *val, char *err, size_t errsz) {
  return set_text(v->cwenc_key_list_a_path, sizeof v->cwenc_key_list_a_path, val, err, errsz);
}

int cas_vendor_set_cwenc_key_list_b(cas_vendor_t *v, const char *val, char *err, size_t errsz) {
  return set_text(v->cwenc_key_list_b_path, sizeof v->cwenc_key_list_b_path, val, err, errsz);
}

int cas_set_algo(cas_algo_t *dst, const char *val, char *err, size_t errsz) {
  static const enum_map_t map[] = {{"cissa", CAS_ALGO_CISSA}, {"csa2", CAS_ALGO_CSA2}, {"csa1", CAS_ALGO_CSA1}};
  int m;
  if (map_lookup(map, sizeof map / sizeof map[0], val, &m)) return bad(err, errsz, val, "cissa|csa2|csa1");
  *dst = (cas_algo_t)m;
  return 0;
}

int cas_set_cp_duration(unsigned *dst, const char *val, char *err, size_t errsz) {
  return argutil_uint_range(val, 1, 86400000, dst) ? bad(err, errsz, val, "ms, 1..86400000") : 0;
}

int cas_set_biss2_sw(int *enabled, unsigned char *sw, const char *val, char *err, size_t errsz) {
  if (biss_parse_hex16(val, sw)) return bad(err, errsz, val, "32 hex chars");
  *enabled = 1;
  return 0;
}

int cas_set_biss2_emit_esw(int *enabled, unsigned char *id, const char *val, char *err, size_t errsz) {
  if (biss_parse_hex16(val, id)) return bad(err, errsz, val, "32 hex chars");
  *enabled = 1;
  return 0;
}

int cas_set_biss1_sw(int *enabled, unsigned char *cw, const char *val, char *err, size_t errsz) {
  if (biss1_parse_sw(val, cw)) return bad(err, errsz, val, "12 hex chars");
  *enabled = 1;
  return 0;
}

int cas_set_biss2_ca_session_id(unsigned *dst, int *given, const char *val, char *err, size_t errsz) {
  char *end;
  unsigned long v = strtoul(val, &end, 0);
  if (*end != '\0' || v > 0xFFFFUL) return bad(err, errsz, val, "16 bit, dec or 0x-hex");
  *dst = (unsigned)v;
  *given = 1;
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
  size_t cwenc_cw_len = scrambler_cw_len(cas_algo == CAS_ALGO_CISSA ? SCRAMBLE_ALGO_CISSA : SCRAMBLE_ALGO_CSA2);
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
    if (v->emmg_reverse_host[0] && v->emmg_port_given) {
      args_err(tool_name, "--cas-emmg-reverse is mutually exclusive with --cas-emmg-port (--cas-ecmg %s:%u)", v->ecmg_host, v->ecmg_port);
      return -1;
    }
    if (v->emmg_reverse_host[0] && v->emmg_max_conns) {
      args_err(tool_name, "--cas-emmg-reverse is mutually exclusive with --cas-emmg-max-conns (--cas-ecmg %s:%u)", v->ecmg_host, v->ecmg_port);
      return -1;
    }
    if (v->cwenc_algorithm[0]) {
      cwenc_config_t cwenc_cfg;
      if (cwenc_config_init(&cwenc_cfg, v->cwenc_algorithm, v->cwenc_aes_mode, v->cwenc_fixed_key_hex, v->cwenc_key_list_a_path, v->cwenc_key_list_b_path) != 0 ||
          cwenc_config_validate(&cwenc_cfg, (int)cwenc_cw_len) != 0) {
        args_err(tool_name, "--cas-ecmg %s:%u: invalid --cas-cwenc-* configuration", v->ecmg_host, v->ecmg_port);
        return -1;
      }
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
