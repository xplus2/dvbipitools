/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */


#include <getopt.h>

#include "lib/helper/argutil.h"
#include "lib/net/jitbuf.h"
#include "priv.h"
#include "lib/net/rist/ristpeer.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const yamlcfg_opt_t opts[] = {
    {'p', "input.pmt-pid", "TS000604", "invalid -p pmt-pid: %s (0x0010..0x1FFE)", 0},
    {'s', "input.sdt", "LS000685", "-s sdt-text too long", 0},
    {OPT_PROVIDER, "input.provider", "LS000685", "--provider text too long", 0},
    {OPT_STRIP, "input.strip", "TS000605", "invalid --strip: %s (comma list of DATA,ECM, or \"none\")", 0},
    {OPT_HBBTV_ORG_ID, "input.hbbtv-org-id", "TS000606", "invalid --hbbtv-org-id: %s", 0},
    {OPT_HBBTV_APP_ID, "input.hbbtv-app-id", "TS000607", "invalid --hbbtv-app-id: %s", 0},
    {OPT_HBBTV_NAME, "input.hbbtv-name", "LS000685", "--hbbtv-name text too long", 0},
    {OPT_SID, "input.sid", "TS000502", "invalid --sid: %s (1..65535)", 0},
    {OPT_RIST_ENCRYPTION_TYPE_IN, "input.rist-encryption-type-in", "TS000552", "invalid --rist-encryption-type-in: %s (" ARGUTIL_RIST_KEY_SIZE_HINT ")", 0},
    {OPT_RIST_SECRET_IN, "input.rist-secret-in", "LS000685", "--rist-secret-in too long", 0},
    {OPT_RIST_CNAME_IN, "input.rist-cname-in", "LS000685", "--rist-cname-in too long", 0},
    {OPT_RIST_BUFFER_IN, "input.rist-buffer-in", "TS000724", "invalid --rist-buffer-in: %s (1..60000 ms)", 0},
    {OPT_RIST_PROFILE_IN, "input.rist-profile-in", "TS000553", "invalid --rist-profile-in: %s (" RIST_PROF_HINT ")", 0},
    {OPT_SRT_PASSPHRASE_IN, "input.srt-passphrase-in", "LS000685", "--srt-passphrase-in too long", 0},
    {OPT_SRT_PBKEYLEN_IN, "input.srt-pbkeylen-in", "TS000359", "invalid --srt-pbkeylen-in: %s (" ARGUTIL_SRT_PBKEYLEN_HINT ")", 0},
    {OPT_SRT_STREAMID_IN, "input.srt-streamid-in", "LS000685", "--srt-streamid-in too long", 0},
    {OPT_SRT_PACKETFILTER_IN, "input.srt-packetfilter-in", "LS000685", "--srt-packetfilter-in too long", 0},
    {OPT_SRT_LATENCY_IN, "input.srt-latency-in", "TS000360", "invalid --srt-latency-in: %s (1..60000 ms)", 0},
    {OPT_AL_FEC_IN, "input.al-fec-in", "TS000727", "invalid --al-fec-in: %s (want L:D, L*D<=400, L<=40)", 0},
    {OPT_AL_FEC_PORT_IN, "input.al-fec-port-in", "TS000728", "invalid --al-fec-port-in: %s", 0},
};

args_status_t tvh_opt_input(tvh_opt_t *p, int c) {
  config_t *cfg = p->cfg;
  dipitvhead_input_t *in = p->in;
  char err[192];

  int r;

  switch (c) {
    case 'p':
      REQUIRE_INPUT("-p/--pmt-pid");
      break;
    case 's':
      REQUIRE_INPUT("-s/--sdt");
      break;
    case OPT_PROVIDER:
      REQUIRE_INPUT("--provider");
      break;
    case OPT_STRIP:
      REQUIRE_INPUT("--strip");
      break;
    case OPT_HBBTV_ORG_ID:
      REQUIRE_INPUT("--hbbtv-org-id");
      break;
    case OPT_HBBTV_APP_ID:
      REQUIRE_INPUT("--hbbtv-app-id");
      break;
    case OPT_HBBTV_NAME:
      REQUIRE_INPUT("--hbbtv-name");
      break;
    case OPT_SID:
      REQUIRE_INPUT("--sid");
      break;
    case OPT_RIST_ENCRYPTION_TYPE_IN:
      REQUIRE_INPUT("--rist-encryption-type-in");
      break;
    case OPT_RIST_SECRET_IN:
      REQUIRE_INPUT("--rist-secret-in");
      break;
    case OPT_RIST_CNAME_IN:
      REQUIRE_INPUT("--rist-cname-in");
      break;
    case OPT_RIST_BUFFER_IN:
      REQUIRE_INPUT("--rist-buffer-in");
      break;
    case OPT_RIST_PROFILE_IN:
      REQUIRE_INPUT("--rist-profile-in");
      break;
    case OPT_SRT_PASSPHRASE_IN:
      REQUIRE_INPUT("--srt-passphrase-in");
      break;
    case OPT_SRT_PBKEYLEN_IN:
      REQUIRE_INPUT("--srt-pbkeylen-in");
      break;
    case OPT_SRT_STREAMID_IN:
      REQUIRE_INPUT("--srt-streamid-in");
      break;
    case OPT_SRT_PACKETFILTER_IN:
      REQUIRE_INPUT("--srt-packetfilter-in");
      break;
    case OPT_SRT_LATENCY_IN:
      REQUIRE_INPUT("--srt-latency-in");
      break;
    case OPT_AL_FEC_IN:
      REQUIRE_INPUT("--al-fec-in");
      break;
    case OPT_AL_FEC_PORT_IN:
      REQUIRE_INPUT("--al-fec-port-in");
      break;
    default:
      break;
  }
  cfg->parse_item.input = 1;
  r = yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), tvh_cfg_keys, tvh_cfg_nkeys, cfg, c, optarg);
  cfg->parse_item.input = 0;
  if (r) return r > 0 ? ARGS_OK : ARGS_ERR;
  switch (c) {
    case 'i':
      if (!p->cli_inputs) {
        cfg->n_inputs = 0;
        p->cli_inputs = 1;
      }
      CHECK(tvh_cfg_add_input(cfg, optarg, err, sizeof err), "-i");
      break;
    case 'I':
      REQUIRE_INPUT("-I/--iface");
      in->iface_in = optarg;
      break;
    case OPT_STRIP_EIT:
      REQUIRE_INPUT("--strip-eit");
      in->strip_eit = 1;
      break;
    case OPT_HBBTV:
      REQUIRE_INPUT("--hbbtv");
      in->hbbtv_url = optarg;
      break;
    case OPT_JITTER_MS:
      REQUIRE_INPUT("--jitter-ms");
      if (argutil_uint_range(optarg, 1, JITBUF_MAX_DELAY_MS, &in->jitter_ms)) {
        argerr("TS000504", "invalid --jitter-ms: %s (1..%d ms)", optarg, JITBUF_MAX_DELAY_MS);
        return ARGS_ERR;
      }
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
