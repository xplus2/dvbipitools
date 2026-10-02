/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include "lib/helper/argutil.h"
#include "lib/helper/uriparse.h"

#include "priv.h"

/* <addr>:<port> or [<addr6>]:<port>, multicast literal required */
int rdh_mcast_parse(const char *s, config_t *cfg) {
  return uriparse_mcast_addrport(s, &cfg->family, cfg->mcast_group, sizeof cfg->mcast_group, &cfg->mcast_port);
}

void mcast_describe(const config_t *cfg, char *buf, size_t n) {
  uriparse_mcast_describe(cfg->family, cfg->mcast_group, cfg->mcast_port, buf, n);
}

int rdh_id_parse(const char *s, unsigned *out) {
  return argutil_uint_range(s, 1, 0xFFFF, out);
}

/* decimal or 0x-hex pid, 0x0001..0x1FFE */
int rdh_pid_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 0);
  if (*end != '\0' || v == 0 || v > 0x1FFE) return -1;
  *out = (unsigned)v;
  return 0;
}

cas_vendor_t *rdh_current_cas_vendor(config_t *cfg, int cli_vendors, const char *flag) {
  if (!cli_vendors) {
    argerr("--%s must follow --cas-ecmg", flag);
    return NULL;
  }
  return &cfg->cas_vendors[cfg->n_cas_vendors - 1];
}
