/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/uriparse.h"

#include "priv.h"


int sds_mcast_parse(const char *s, config_t *cfg) {
  return uriparse_mcast_addrport(s, &cfg->family, cfg->mcast_group, sizeof cfg->mcast_group, &cfg->mcast_port);
}

int sds_ret_addr_parse(const char *s, char *addr_out, size_t addr_cap, unsigned *port_out) {
  int family;
  return argutil_addrport_parse(s, &family, addr_out, addr_cap, port_out);
}

void mcast_describe(const config_t *cfg, char *buf, size_t n) {
  uriparse_mcast_describe(cfg->family, cfg->mcast_group, cfg->mcast_port, buf, n);
}

int sds_has_suffix(const char *s, const char *sfx) {
  size_t ls = strlen(s), lx = strlen(sfx);
  return ls >= lx && !strcmp(s + ls - lx, sfx);
}
