/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "priv.h"
#include "../segstore.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"
#include "lib/helper/strutil.h"

static const yamlcfg_opt_t opts[] = {
    {OPT_SEGMENT_SIZE, "segment-size", "YS000652", "invalid --segment-size: %s (seconds, min 2)", 0},
    {OPT_SEGMENT_COUNT, "segment-count", "YS000653", "invalid --segment-count: %s (3-" STRINGIFY(HLS_MAX_SEGS) ")", 0},
    {OPT_HLS_PART_SIZE, "hls.part-size", "YS000654", "invalid --hls-part-size: %s (seconds, 0.05-5.0)", 0},
    {OPT_DASH_PART_SIZE, "dash.part-size", "YS000655", "invalid --dash-part-size: %s (seconds, 0.05-5.0)", 0},
    {OPT_DASH_UTC_URL, "dash.utc-url", "YS000656", "invalid --dash-utc-url: too long (max 256 chars)", 0},
    {OPT_HLS_SEG_POOL, "hls.seg-pool", "YS000657", "invalid --hls-seg-pool: %s (min 1)", 0},
    {'f', "format", "YS000658", "invalid -f/--format: %s (comma-separated list of ts,spts,rawaudio,hls,llhls,dash,lldash)", 0},
    {OPT_AL_FEC, "al-fec", "YS000154", "invalid --al-fec: %s (want L:D, L*D<=400, L<=40)", 0},
};

args_status_t dixy_opt_stream(dixy_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), dixy_cfg_keys, dixy_cfg_nkeys, cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1:
      args_free(cfg);
      return ARGS_ERR;
    default: break;
  }
  switch (c) {
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
