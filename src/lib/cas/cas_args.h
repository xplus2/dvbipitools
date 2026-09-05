/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_CAS_CAS_ARGS_H
#define LIB_CAS_CAS_ARGS_H

#include <stddef.h>

/* 32-bit Super_CAS_id, hex or decimal */
int cas_super_id_parse(const char *s, unsigned *out);

/* tcp://host:port/ or host:port. brackets required for literal IPv6 host, e.g. [::1]:2222 */
int cas_endpoint_parse(const char *s, char *host_out, size_t host_out_sz, unsigned *port_out);

/* 2 or 3, ETSI TS 103 197 Simulcrypt protocol versions, no other value valid */
int cas_version_parse(const char *s, unsigned *out);

/* NONE = --cas-algo not given, CAS disabled */
typedef enum { CAS_ALGO_NONE, CAS_ALGO_CISSA, CAS_ALGO_CSA2, CAS_ALGO_CSA1 } cas_algo_t;

typedef enum { CAS_OUTAGE_FROZEN, CAS_OUTAGE_CYCLING, CAS_OUTAGE_SILENT } cas_outage_mode_t;

#define ARGS_MAX_CAS_VENDORS 8 /* matches CAS_GROUP_MAX_VENDORS */

typedef struct {
  char ecmg_host[256];           /* --cas-ecmg */
  unsigned ecmg_port;            /* --cas-ecmg */
  unsigned ecmg_version;         /* --cas-ecmg-version right after this --cas-ecmg; 0 = auto-negotiate v2/v3 */
  unsigned super_cas_id;         /* --cas-super-id right after this --cas-ecmg */
  unsigned ecm_id;               /* --cas-ecm-id right after this --cas-ecmg */
  unsigned ecm_pid;              /* --cas-ecm-pid right after this --cas-ecmg; default 0x0020 */
  unsigned emmg_port;            /* --cas-emmg-port right after this --cas-ecmg; default 8002 */
  unsigned emmg_max_conns;       /* --cas-emmg-max-conns right after this --cas-ecmg. 0 = default (8) */
  unsigned emmg_version;         /* --cas-emmg-version right after this --cas-ecmg; 0 = accept client's proposal */
  unsigned emm_pid;              /* --cas-emm-pid right after this --cas-ecmg; default 0x0021 */
  cas_outage_mode_t resilience;  /* --cas-resilience right after this --cas-ecmg; default frozen */
  int required;                  /* --cas-required right after this --cas-ecmg */
  char cwenc_algorithm[8];
  char cwenc_aes_mode[8];
  char cwenc_fixed_key_hex[65];
  char cwenc_key_list_a_path[256];
  char cwenc_key_list_b_path[256];
} cas_vendor_t;

/* biss2/biss1/biss2-ca/simulcrypt mutual exclusivity, plus per-vendor cross-checks
   (super-id/ecm-id required, ecm/emm pid collisions, emmg port reuse). errors go through
   argutil_verr() tagged with tool_name. 0 ok, -1 err (message already printed) */
int cas_args_validate(const char *tool_name, cas_algo_t cas_algo, const cas_vendor_t *vendors, unsigned n_vendors,
                       int biss2_enabled, int biss1_enabled, int biss2_ca_enabled, int biss2_emit_esw,
                       int biss2_ca_session_id_given, unsigned cas_cp_duration_ms);

#endif
