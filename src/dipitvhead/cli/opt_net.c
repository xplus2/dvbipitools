/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */


#include <getopt.h>

#include "lib/helper/argutil.h"
#include "lib/net/netconnect.h"
#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const yamlcfg_opt_t opts[] = {
    {'m', "mcast", "TS000103", "invalid -m group:port: %s", 0},
    {'T', "ttl", "TS000495", "invalid -T ttl: %s (1..255)", 0},
    {OPT_AL_FEC, "al-fec", "TS000154", "invalid --al-fec: %s (want L:D, L*D<=400, L<=40)", 0},
    {OPT_AL_FEC_PORT, "al-fec-port", "TS000155", "invalid --al-fec-port: %s", 0},
    {OPT_RIST_SECRET, "rist.secret", "LS000685", "--rist-secret too long", 0},
    {OPT_RIST_ENCRYPTION_TYPE, "rist.encryption-type", "TS000358", "invalid --rist-encryption-type: %s (" ARGUTIL_RIST_KEY_SIZE_HINT ")", 0},
    {OPT_RIST_CNAME, "rist.cname", "LS000685", "--rist-cname too long", 0},
    {OPT_RIST_BUFFER, "rist.buffer", "TS000511", "invalid --rist-buffer: %s (1..60000 ms)", 0},
    {OPT_SRT_PASSPHRASE, "srt.passphrase", "LS000685", "--srt-passphrase too long", 0},
    {OPT_SRT_PBKEYLEN, "srt.pbkeylen", "TS000361", "invalid --srt-pbkeylen: %s (" ARGUTIL_SRT_PBKEYLEN_HINT ")", 0},
    {OPT_SRT_STREAMID, "srt.streamid", "LS000685", "--srt-streamid too long", 0},
    {OPT_SRT_PACKETFILTER, "srt.packetfilter", "LS000685", "--srt-packetfilter too long", 0},
    {OPT_SRT_LATENCY, "srt.latency", "TS000362", "invalid --srt-latency: %s (1..60000 ms)", 0},
};

args_status_t tvh_opt_net(tvh_opt_t *p, int c) {
  config_t *cfg = p->cfg;
  char err[192];

  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), tvh_cfg_keys, tvh_cfg_nkeys, cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1: return ARGS_ERR;
    default: break;
  }
  switch (c) {
    case 'O':
      cfg->iface_out = optarg;
      break;
    case 'u':
      cfg->rtp = 0;
      break;
    case OPT_DSCP:
      if (argutil_int_opt(TOOL_NAME, "TS000106", "dscp", optarg, net_dscp_parse, NET_DSCP_NAMES, &cfg->dscp)) return ARGS_ERR;
      break;
    case 'R':
      if (!p->cli_peers) {
        cfg->n_rist = 0;
        cfg->n_srt = 0;
        p->cli_peers = 1;
      }
      CHECK(tvh_cfg_add_peer(cfg, optarg, err, sizeof err), "-R");
      break;
    case OPT_RIST_PROFILE:
      CHECK(tvh_cfg_profile(cfg, optarg, err, sizeof err), "--rist-profile");
      break;
    case OPT_SRT_GROUP_MODE:
      CHECK(tvh_cfg_group_mode(cfg, optarg, err, sizeof err), "--srt-group-mode");
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
