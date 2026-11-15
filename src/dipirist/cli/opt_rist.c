/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"
#include "lib/net/rist/ristpeer.h"
#include "priv.h"

static const yamlcfg_opt_t opts[] = {
    {OPT_PROFILE, "profile", "VS000151", "invalid --profile: %s (" RIST_PROF_HINT ")", 0},
    {OPT_SECRET, "secret", "LS000685", "--secret too long", 0},
    {OPT_ENCRYPTION_TYPE, "encryption-type", "VS000152", "invalid --encryption-type: %s (" ARGUTIL_RIST_KEY_SIZE_HINT ")", 0},
    {OPT_CNAME, "cname", "LS000685", "--cname too long", 0},
    {OPT_BUFFER, "buffer", "VS000153", "invalid --buffer: %s (1..60000 ms)", 0},
    {OPT_AL_FEC, "al-fec", "VS000154", "invalid --al-fec: %s (want L:D, L*D<=400, L<=40)", 0},
    {OPT_AL_FEC_PORT, "al-fec-port", "VS000155", "invalid --al-fec-port: %s", 0},
    {OPT_TTL, "ttl", "VS000558", "invalid --ttl: %s (0..255)", 0},
};

args_status_t dipirist_opt_rist(dipirist_opt_t *p, int c) {
  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), dipirist_cfg_keys, dipirist_cfg_nkeys, p->cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1: return ARGS_ERR;
    default: return OPT_UNHANDLED;
  }
}
