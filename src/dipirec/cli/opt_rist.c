/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "lib/helper/argutil.h"
#include "priv.h"
#include "lib/net/rist/ristpeer.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const yamlcfg_opt_t opts[] = {
    {OPT_PROFILE, "rist.profile", "CS000357", "invalid --rist-profile: %s (" RIST_PROF_HINT ")", 0},
    {OPT_SECRET, "rist.secret", "LS000685", "--rist-secret too long", 0},
    {OPT_ENCRYPTION_TYPE, "rist.encryption-type", "CS000358", "invalid --rist-encryption-type: %s (" ARGUTIL_RIST_KEY_SIZE_HINT ")", 0},
    {OPT_ENCRYPTION_TYPE_IN, "rist.encryption-type-in", "CS000552", "invalid --rist-encryption-type-in: %s (" ARGUTIL_RIST_KEY_SIZE_HINT ")", 0},
    {OPT_CNAME, "rist.cname", "LS000685", "--rist-cname too long", 0},
    {OPT_BUFFER, "rist.buffer", "CS000511", "invalid --rist-buffer: %s (ms)", 0},
    {OPT_PROFILE_IN, "rist.profile-in", "CS000553", "invalid --rist-profile-in: %s (" RIST_PROF_HINT ")", 0},
};

args_status_t rec_opt_rist(rec_opt_t *p, int c) {
  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), rec_cfg_keys, rec_cfg_nkeys, p->cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1: return ARGS_ERR;
    default: return OPT_UNHANDLED;
  }
}
