/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/uriparse.h"
#include "lib/net/netconnect.h"

#include "priv.h"

args_status_t dscr_opt_net(dscr_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case OPT_PROFILE:
      if (dscr_cfg_profile(cfg, optarg)) {
        argerr("invalid --rist-profile: %s (simple|main)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_ENCRYPTION_TYPE:
      if (argutil_rist_key_size(optarg, &cfg->rist_key_size)) {
        argerr("invalid --rist-encryption-type: %s (128|256)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_SRT_PASSPHRASE_IN:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_passphrase_in, sizeof cfg->srt_passphrase_in, optarg, "--srt-passphrase-in"))
        return ARGS_ERR;
      break;
    case OPT_SRT_PBKEYLEN_IN: {
      char *end;
      unsigned long v = strtoul(optarg, &end, 10);
      if (*end != '\0' || (v != 16 && v != 24 && v != 32)) {
        argerr("invalid --srt-pbkeylen-in: %s (16|24|32)", optarg);
        return ARGS_ERR;
      }
      cfg->srt_pbkeylen_in = (int)v;
      break;
    }
    case OPT_SRT_STREAMID_IN:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_streamid_in, sizeof cfg->srt_streamid_in, optarg, "--srt-streamid-in"))
        return ARGS_ERR;
      break;
    case OPT_SRT_PACKETFILTER_IN:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_packetfilter_in, sizeof cfg->srt_packetfilter_in, optarg, "--srt-packetfilter-in"))
        return ARGS_ERR;
      break;
    case OPT_SRT_LATENCY_IN: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, 60000, &v)) {
        argerr("invalid --srt-latency-in: %s (1..60000 ms)", optarg);
        return ARGS_ERR;
      }
      cfg->srt_latency_in_ms = v;
      break;
    }
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
