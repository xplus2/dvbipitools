/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_CAS_CAS_ARGS_H
#define LIB_CAS_CAS_ARGS_H

#include <stddef.h>

/* 32-bit Super_CAS_id, conventionally written in hex; decimal also accepted */
int cas_super_id_parse(const char *s, unsigned *out);

/* tcp://host:port/ or host:port; brackets required for a literal IPv6 host, e.g. [::1]:2222 */
int cas_endpoint_parse(const char *s, char *host_out, size_t host_out_sz, unsigned *port_out);

/* 2 or 3, ETSI TS 103 197 Simulcrypt protocol versions; no other value is valid */
int cas_version_parse(const char *s, unsigned *out);

#endif
