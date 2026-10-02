/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/biss/biss.h"
#include "lib/cas/cas_args.h"
#include "lib/cas/emmg_server/emmg_server.h"
#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/mux/fec2022.h"
#include "lib/net/netconnect.h"

#include "priv.h"

args_status_t rdh_opt_net(rdh_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
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
      if (strncmp(optarg, "rist://", 7) == 0) {
        if (cfg->n_srt > 0) {
          argerr("-R: rist:// and srt:// peers cannot mix in one run");
          return ARGS_ERR;
        }
        if (cfg->n_rist >= ARGS_MAX_RIST_PEERS) {
          argerr("too many -R peers (max %d)", ARGS_MAX_RIST_PEERS);
          return ARGS_ERR;
        }
        if (argutil_bufcpy_opt(TOOL_NAME, cfg->rist_uri[cfg->n_rist], sizeof cfg->rist_uri[0], optarg, "-R rist uri"))
          return ARGS_ERR;
        cfg->n_rist++;
      } else if (strncmp(optarg, "srt://", 6) == 0) {
        if (cfg->n_rist > 0) {
          argerr("-R: rist:// and srt:// peers cannot mix in one run");
          return ARGS_ERR;
        }
        if (cfg->n_srt >= ARGS_MAX_SRT_PEERS) {
          argerr("too many -R srt:// peers (max %d)", ARGS_MAX_SRT_PEERS);
          return ARGS_ERR;
        }
        if (optarg[6] == '@') {
          argerr("-R srt:// output always calls out, no listener mode");
          return ARGS_ERR;
        }
        if (argutil_addrport_parse(optarg + 6, &cfg->srt_family[cfg->n_srt], cfg->srt_host[cfg->n_srt], sizeof cfg->srt_host[0], &cfg->srt_port[cfg->n_srt])) {
          argerr("invalid -R srt uri: %s", optarg);
          return ARGS_ERR;
        }
        cfg->n_srt++;
      } else {
        argerr("invalid -R uri: %s (must start with rist:// or srt://)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_RIST_PROFILE:
      p->profile_arg = optarg;
      break;
    case OPT_RIST_SECRET:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->rist_secret, sizeof cfg->rist_secret, optarg, "--rist-secret"))
        return ARGS_ERR;
      p->have_secret = 1;
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
    case OPT_RIST_BUFFER: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
        argerr("invalid --rist-buffer: %s (ms)", optarg);
        return ARGS_ERR;
      }
      cfg->rist_buffer_ms = v;
      break;
    }
    case OPT_SRT_GROUP_MODE:
      p->srt_group_mode_arg = optarg;
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
    case OPT_SRT_LATENCY: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, 60000, &v)) {
        argerr("invalid --srt-latency: %s (1..60000 ms)", optarg);
        return ARGS_ERR;
      }
      cfg->srt_latency_ms = v;
      break;
    }
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
