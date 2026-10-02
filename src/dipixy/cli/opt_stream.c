/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"
#include "lib/mux/fec2022.h"

#include "priv.h"

args_status_t dixy_opt_stream(dixy_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case OPT_SEGMENT_SIZE: {
      char *end;
      double v = strtod(optarg, &end);
      if (*end != '\0' || !isfinite(v) || v < 2.0 || v > 1e9) {
        argerr("invalid --segment-size: %s (seconds, min 2)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->segment_size = v;
      break;
    }
    case OPT_SEGMENT_COUNT: {
      unsigned v;
      if (argutil_uint_range(optarg, 3, 1000, &v)) {
        argerr("invalid --segment-count: %s (min 3)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->segment_count = (int)v;
      break;
    }
    case OPT_HLS_PART_SIZE: {
      char *end;
      double v = strtod(optarg, &end);
      if (*end != '\0' || !isfinite(v) || v < 0.05 || v > 5.0) {
        argerr("invalid --hls-part-size: %s (seconds, 0.05-5.0)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->hls_part_size = v;
      break;
    }
    case OPT_DASH_PART_SIZE: {
      char *end;
      double v = strtod(optarg, &end);
      if (*end != '\0' || !isfinite(v) || v < 0.05 || v > 5.0) {
        argerr("invalid --dash-part-size: %s (seconds, 0.05-5.0)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->dash_part_size = v;
      break;
    }
    case OPT_DASH_UTC_URL:
      if (strlen(optarg) > 256) {
        argerr("invalid --dash-utc-url: too long (max 256 chars)");
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->dash_utc_url = optarg;
      break;
    case OPT_HLS_SEG_POOL: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, INT_MAX, &v)) {
        argerr("invalid --hls-seg-pool: %s (min 1)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->hls_seg_pool = (int)v;
      break;
    }
    case 'f':
      if (dixy_cfg_format(cfg, optarg)) {
        argerr("invalid -f/--format: %s (comma-separated list of ts,spts,rawaudio,hls,llhls,dash,lldash)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      break;
    case OPT_NO_URL_RTP:
      cfg->no_url_rtp = 1;
      break;
    case OPT_NO_URL_UDP:
      cfg->no_url_udp = 1;
      break;
    case OPT_NO_URL_SRT:
      cfg->no_url_srt = 1;
      break;
    case OPT_NO_PID_FILTERS:
      cfg->no_pid_filters = 1;
      break;
    case OPT_NO_LCEVC:
      cfg->no_lcevc = 1;
      break;
    case OPT_NO_FCC:
      cfg->no_fcc = 1;
      break;
    case OPT_NO_RET:
      cfg->no_ret = 1;
      break;
    case OPT_AL_FEC:
      if (fec2022_parse_ld(optarg, &cfg->al_fec_l, &cfg->al_fec_d)) {
        argerr("invalid --al-fec: %s (want L:D, L*D<=400, L<=40)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      break;
    case OPT_NO_AL_FEC:
      cfg->no_al_fec = 1;
      break;
    case OPT_NO_STATUS:
      cfg->no_status = 1;
      break;
    case OPT_STATUS_TPL:
      cfg->status_template = optarg;
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
