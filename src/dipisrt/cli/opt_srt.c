/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/mux/fec2022.h"
#include "lib/helper/uriparse.h"
#include "lib/net/netconnect.h"

#include "priv.h"

args_status_t srt_opt_srt(srt_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case OPT_GROUP_MODE: {
#ifndef DIPISRT_HAVE_BONDING
      argerr("--group-mode needs a libsrt built with bonding support (ENABLE_BONDING=ON)");
      return ARGS_ERR;
#else
      static const enum_map_t map[] = {{"broadcast", SRTGROUP_BROADCAST}, {"backup", SRTGROUP_BACKUP}};
      int v;
      if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
        argerr("invalid --group-mode: %s (broadcast|backup)", optarg);
        return ARGS_ERR;
      }
      cfg->group_mode = (srtgroup_mode_t)v;
      break;
#endif
    }
    case OPT_RENDEZVOUS:
      cfg->rendezvous = 1;
      break;
    case OPT_LOCAL: {
      int family_unused;
      if (argutil_addrport_parse(optarg, &family_unused, cfg->local_host, sizeof cfg->local_host, &cfg->local_port)) {
        argerr("invalid --local: %s", optarg);
        return ARGS_ERR;
      }
      break;
    }
    case OPT_PASSPHRASE:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->passphrase, sizeof cfg->passphrase, optarg, "--passphrase"))
        return ARGS_ERR;
      break;
    case OPT_PBKEYLEN: {
      char *end;
      unsigned long v = strtoul(optarg, &end, 10);
      if (*end != '\0' || (v != 16 && v != 24 && v != 32)) {
        argerr("invalid --pbkeylen: %s (16|24|32)", optarg);
        return ARGS_ERR;
      }
      cfg->pbkeylen = (int)v;
      break;
    }
    case OPT_STREAMID:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->streamid, sizeof cfg->streamid, optarg, "--streamid"))
        return ARGS_ERR;
      break;
    case OPT_PACKETFILTER:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->packetfilter, sizeof cfg->packetfilter, optarg, "--packetfilter"))
        return ARGS_ERR;
      break;
    case OPT_LATENCY:
      if (argutil_uint_range(optarg, 1, 60000, &cfg->latency_ms)) {
        argerr("invalid --latency: %s (1..60000 ms)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_SEND_BUFFER_MULT:
      if (argutil_uint_range(optarg, 1, 32, &cfg->send_buffer_mult)) {
        argerr("invalid --send-buffer-mult: %s (1..32)", optarg);
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
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
