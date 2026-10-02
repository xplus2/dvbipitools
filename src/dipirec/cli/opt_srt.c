/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"
#include "lib/mux/fec2022.h"

#include "priv.h"

args_status_t rec_opt_srt(rec_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case OPT_SRT_PASSPHRASE_IN:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_passphrase_in, sizeof cfg->srt_passphrase_in, optarg, "--srt-passphrase-in"))
        return ARGS_ERR;
      break;
    case OPT_SRT_PBKEYLEN_IN:
      if (rec_parse_pbkeylen_opt(optarg, &cfg->srt_pbkeylen_in, "--srt-pbkeylen-in"))
        return ARGS_ERR;
      break;
    case OPT_SRT_STREAMID_IN:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_streamid_in, sizeof cfg->srt_streamid_in, optarg, "--srt-streamid-in"))
        return ARGS_ERR;
      break;
    case OPT_SRT_PACKETFILTER_IN:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_packetfilter_in, sizeof cfg->srt_packetfilter_in, optarg, "--srt-packetfilter-in"))
        return ARGS_ERR;
      break;
    case OPT_SRT_LATENCY_IN:
      if (argutil_uint_range(optarg, 1, 60000, &cfg->srt_latency_in_ms)) {
        argerr("invalid --srt-latency-in: %s (1..60000 ms)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_SRT_PASSPHRASE:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_passphrase, sizeof cfg->srt_passphrase, optarg, "--srt-passphrase"))
        return ARGS_ERR;
      break;
    case OPT_SRT_PBKEYLEN:
      if (rec_parse_pbkeylen_opt(optarg, &cfg->srt_pbkeylen, "--srt-pbkeylen"))
        return ARGS_ERR;
      break;
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
