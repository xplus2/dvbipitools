/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */


#include <getopt.h>
#include <limits.h>
#include <stdlib.h>

#include "lib/helper/argutil.h"
#include "lib/mux/fec2022.h"
#include "lib/net/netconnect.h"

#include "priv.h"

args_status_t tvh_opt_net(tvh_opt_t *p, int c) {
  config_t *cfg = p->cfg;
  char err[192];

  switch (c) {
    case 'm':
      if (tvh_cfg_mcast(cfg, optarg)) {
        argerr("invalid -m group:port: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case 'O':
      cfg->iface_out = optarg;
      break;
    case 'u':
      cfg->rtp = 0;
      break;
    case 'T': {
      unsigned v;
      if (argutil_uint_range(optarg, 1, 255, &v)) {
        argerr("invalid -T ttl: %s (1..255)", optarg);
        return ARGS_ERR;
      }
      cfg->ttl = v;
      break;
    }
    case OPT_DSCP:
      if (net_dscp_parse(optarg, &cfg->dscp)) {
        argerr("invalid --dscp: %s (video-high|video-low|voice|signalling|best-effort|0..63)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_AL_FEC:
      if (fec2022_parse_ld(optarg, &cfg->al_fec_l, &cfg->al_fec_d)) {
        argerr("invalid --al-fec: %s (want L:D, L*D<=400, L<=40)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_AL_FEC_PORT:
      if (argutil_port_parse(optarg, &cfg->al_fec_port)) {
        argerr("invalid --al-fec-port: %s", optarg);
        return ARGS_ERR;
      }
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
    case OPT_RIST_SECRET:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->rist_secret, sizeof cfg->rist_secret, optarg, "--rist-secret"))
        return ARGS_ERR;
      break;
    case OPT_RIST_ENCRYPTION_TYPE:
      if (argutil_rist_key_size(optarg, &cfg->rist_key_size)) {
        argerr("invalid --rist-encryption-type: %s (128|256)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_RIST_CNAME:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->rist_cname, sizeof cfg->rist_cname, optarg, "--rist-cname"))
        return ARGS_ERR;
      break;
    case OPT_RIST_BUFFER:
      if (argutil_uint_range(optarg, 1, UINT_MAX, &cfg->rist_buffer_ms)) {
        argerr("invalid --rist-buffer: %s (ms)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_SRT_GROUP_MODE:
      CHECK(tvh_cfg_group_mode(cfg, optarg, err, sizeof err), "--srt-group-mode");
      break;
    case OPT_SRT_PASSPHRASE:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_passphrase, sizeof cfg->srt_passphrase, optarg, "--srt-passphrase"))
        return ARGS_ERR;
      break;
    case OPT_SRT_PBKEYLEN: {
      char *end;
      unsigned long v = strtoul(optarg, &end, 10);
      if (*end != '\0' || (v != 16 && v != 24 && v != 32)) {
        argerr("invalid --srt-pbkeylen: %s (16|24|32)", optarg);
        return ARGS_ERR;
      }
      cfg->srt_pbkeylen = (int)v;
      break;
    }
    case OPT_SRT_STREAMID:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_streamid, sizeof cfg->srt_streamid, optarg, "--srt-streamid"))
        return ARGS_ERR;
      break;
    case OPT_SRT_PACKETFILTER:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_packetfilter, sizeof cfg->srt_packetfilter, optarg, "--srt-packetfilter"))
        return ARGS_ERR;
      break;
    case OPT_SRT_LATENCY:
      if (argutil_uint_range(optarg, 1, 60000, &cfg->srt_latency_ms)) {
        argerr("invalid --srt-latency: %s (1..60000 ms)", optarg);
        return ARGS_ERR;
      }
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
