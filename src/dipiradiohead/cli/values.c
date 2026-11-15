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

cas_vendor_t *rdh_current_cas_vendor(config_t *cfg, int cli_vendors, const char *flag) {
  if (!cli_vendors) {
    argerr("RS000512", "--%s must follow --cas-ecmg", flag);
    return NULL;
  }
  return &cfg->cas_vendors[cfg->n_cas_vendors - 1];
}
