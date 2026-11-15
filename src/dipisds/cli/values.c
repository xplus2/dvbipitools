/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "lib/helper/argutil.h"
#include "lib/helper/uriparse.h"

#include "priv.h"


void mcast_describe(const config_t *cfg, char *buf, size_t n) {
  uriparse_mcast_describe(cfg->family, cfg->mcast_group, cfg->mcast_port, buf, n);
}

int sds_has_suffix(const char *s, const char *sfx) {
  size_t ls = strlen(s);
  size_t lx = strlen(sfx);
  return ls >= lx && !strcasecmp(s + ls - lx, sfx);
}
