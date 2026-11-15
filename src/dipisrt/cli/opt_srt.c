/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "lib/config/yamlcfg.h"
#include "lib/helper/argutil.h"
#include "lib/helper/growutil.h"
#include "priv.h"

static const yamlcfg_opt_t opts[] = {
    {OPT_LOCAL, "local", "HS000243", "invalid --local: %s", 0},
    {OPT_PASSPHRASE, "passphrase", "LS000685", "--passphrase too long", 0},
    {OPT_PBKEYLEN, "pbkeylen", "HS000244", "invalid --pbkeylen: %s (" ARGUTIL_SRT_PBKEYLEN_HINT ")", 0},
    {OPT_STREAMID, "streamid", "LS000685", "--streamid too long", 0},
    {OPT_PACKETFILTER, "packetfilter", "LS000685", "--packetfilter too long", 0},
    {OPT_LATENCY, "latency", "HS000245", "invalid --latency: %s (1..60000 ms)", 0},
    {OPT_SEND_BUFFER_MULT, "send-buffer-mult", "HS000246", "invalid --send-buffer-mult: %s (1..32)", 0},
    {OPT_AL_FEC, "al-fec", "HS000154", "invalid --al-fec: %s (want L:D, L*D<=400, L<=40)", 0},
    {OPT_AL_FEC_PORT, "al-fec-port", "HS000155", "invalid --al-fec-port: %s", 0},
#ifdef DIPISRT_HAVE_BONDING
    {OPT_GROUP_MODE, "group-mode", "HS000242", "invalid --group-mode: %s (broadcast|backup)", 0},
#endif
};

args_status_t srt_opt_srt(srt_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), srt_cfg_keys, srt_cfg_nkeys, cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1: return ARGS_ERR;
    default: break;
  }
  switch (c) {
#ifndef DIPISRT_HAVE_BONDING
    case OPT_GROUP_MODE:
      argerr("HS000241", "--group-mode needs a libsrt built with bonding support (ENABLE_BONDING=ON)");
      return ARGS_ERR;
#endif
    case OPT_RENDEZVOUS:
      cfg->rendezvous = 1;
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
