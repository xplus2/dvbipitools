/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "lib/helper/argutil.h"
#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const yamlcfg_opt_t opts[] = {
    {OPT_SRT_PASSPHRASE_IN, "srt.passphrase-in", "LS000685", "--srt-passphrase-in too long", 0},
    {OPT_SRT_PBKEYLEN_IN, "srt.pbkeylen-in", "CS000559", "invalid --srt-pbkeylen-in: %s (" ARGUTIL_SRT_PBKEYLEN_HINT ")", 0},
    {OPT_SRT_STREAMID_IN, "srt.streamid-in", "LS000685", "--srt-streamid-in too long", 0},
    {OPT_SRT_PACKETFILTER_IN, "srt.packetfilter-in", "LS000685", "--srt-packetfilter-in too long", 0},
    {OPT_SRT_LATENCY_IN, "srt.latency-in", "CS000360", "invalid --srt-latency-in: %s (1..60000 ms)", 0},
    {OPT_SRT_PASSPHRASE, "srt.passphrase", "LS000685", "--srt-passphrase too long", 0},
    {OPT_SRT_PBKEYLEN, "srt.pbkeylen", "CS000559", "invalid --srt-pbkeylen: %s (" ARGUTIL_SRT_PBKEYLEN_HINT ")", 0},
    {OPT_SRT_STREAMID, "srt.streamid", "LS000685", "--srt-streamid too long", 0},
    {OPT_SRT_PACKETFILTER, "srt.packetfilter", "LS000685", "--srt-packetfilter too long", 0},
    {OPT_SRT_LATENCY, "srt.latency", "CS000362", "invalid --srt-latency: %s (1..60000 ms)", 0},
};

args_status_t rec_opt_srt(rec_opt_t *p, int c) {
  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), rec_cfg_keys, rec_cfg_nkeys, p->cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1: return ARGS_ERR;
    default: return OPT_UNHANDLED;
  }
}
