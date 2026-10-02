/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */


#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/net/jitbuf.h"

#include "priv.h"

args_status_t tvh_opt_input(tvh_opt_t *p, int c) {
  config_t *cfg = p->cfg;
  dipitvhead_input_t *in = p->in;
  char err[192];

  switch (c) {
    case 'i':
      if (!p->cli_inputs) {
        cfg->n_inputs = 0;
        p->cli_inputs = 1;
      }
      CHECK(tvh_cfg_add_input(cfg, optarg, err, sizeof err), "-i");
      break;
    case 'p':
      REQUIRE_INPUT("-p/--pmt-pid");
      if (tvh_cfg_pid(optarg, &in->pmt_pid) || in->pmt_pid == 0) {
        argerr("invalid -p pmt-pid: %s (0x0010..0x1FFE)", optarg);
        return ARGS_ERR;
      }
      break;
    case 'I':
      REQUIRE_INPUT("-I/--iface");
      in->iface_in = optarg;
      break;
    case 's':
      REQUIRE_INPUT("-s/--sdt");
      if (strcmp(optarg, "-") == 0) {
        in->sdt_mode = TABLE_DROP;
      } else {
        in->sdt_mode = TABLE_OVERRIDE;
        if (argutil_bufcpy_opt(TOOL_NAME, in->sdt_text, sizeof in->sdt_text, optarg, "-s sdt-text"))
          return ARGS_ERR;
      }
      break;
    case OPT_PROVIDER:
      REQUIRE_INPUT("--provider");
      if (argutil_bufcpy_opt(TOOL_NAME, in->provider_text, sizeof in->provider_text, optarg, "--provider text"))
        return ARGS_ERR;
      break;
    case OPT_STRIP_EIT:
      REQUIRE_INPUT("--strip-eit");
      in->strip_eit = 1;
      break;
    case OPT_STRIP:
      REQUIRE_INPUT("--strip");
      if (tvh_cfg_strip(&in->strip_mask, optarg)) {
        argerr("invalid --strip: %s (comma list of DATA,ECM, or \"none\")", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_HBBTV:
      REQUIRE_INPUT("--hbbtv");
      in->hbbtv_url = optarg;
      break;
    case OPT_HBBTV_ORG_ID:
      REQUIRE_INPUT("--hbbtv-org-id");
      if (tvh_org_id_parse(optarg, &in->hbbtv_org_id)) {
        argerr("invalid --hbbtv-org-id: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_HBBTV_APP_ID:
      REQUIRE_INPUT("--hbbtv-app-id");
      if (tvh_id_parse(optarg, &in->hbbtv_app_id)) {
        argerr("invalid --hbbtv-app-id: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_SID:
      REQUIRE_INPUT("--sid");
      if (tvh_id_parse(optarg, &in->sid)) {
        argerr("invalid --sid: %s (1..65535)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_RIST_ENCRYPTION_TYPE_IN:
      REQUIRE_INPUT("--rist-encryption-type-in");
      if (argutil_rist_key_size(optarg, &in->rist_key_size_in)) {
        argerr("invalid --rist-encryption-type-in: %s (128|256)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_RIST_PROFILE_IN: {
      static const enum_map_t map[] = {{"simple", 0}, {"main", 1}};
      int v;
      REQUIRE_INPUT("--rist-profile-in");
      if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
        argerr("invalid --rist-profile-in: %s (simple|main)", optarg);
        return ARGS_ERR;
      }
      in->rist_profile_main = v;
      break;
    }
    case OPT_SRT_PASSPHRASE_IN:
      REQUIRE_INPUT("--srt-passphrase-in");
      if (argutil_bufcpy_opt(TOOL_NAME, in->srt_passphrase_in, sizeof in->srt_passphrase_in, optarg, "--srt-passphrase-in"))
        return ARGS_ERR;
      break;
    case OPT_SRT_PBKEYLEN_IN: {
      char *end;
      unsigned long v;
      REQUIRE_INPUT("--srt-pbkeylen-in");
      v = strtoul(optarg, &end, 10);
      if (*end != '\0' || (v != 16 && v != 24 && v != 32)) {
        argerr("invalid --srt-pbkeylen-in: %s (16|24|32)", optarg);
        return ARGS_ERR;
      }
      in->srt_pbkeylen_in = (int)v;
      break;
    }
    case OPT_SRT_STREAMID_IN:
      REQUIRE_INPUT("--srt-streamid-in");
      if (argutil_bufcpy_opt(TOOL_NAME, in->srt_streamid_in, sizeof in->srt_streamid_in, optarg, "--srt-streamid-in"))
        return ARGS_ERR;
      break;
    case OPT_SRT_PACKETFILTER_IN:
      REQUIRE_INPUT("--srt-packetfilter-in");
      if (argutil_bufcpy_opt(TOOL_NAME, in->srt_packetfilter_in, sizeof in->srt_packetfilter_in, optarg, "--srt-packetfilter-in"))
        return ARGS_ERR;
      break;
    case OPT_SRT_LATENCY_IN:
      REQUIRE_INPUT("--srt-latency-in");
      if (argutil_uint_range(optarg, 1, 60000, &in->srt_latency_in_ms)) {
        argerr("invalid --srt-latency-in: %s (1..60000 ms)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_JITTER_MS:
      REQUIRE_INPUT("--jitter-ms");
      if (argutil_uint_range(optarg, 1, JITBUF_MAX_DELAY_MS, &in->jitter_ms)) {
        argerr("invalid --jitter-ms: %s (1..%d ms)", optarg, JITBUF_MAX_DELAY_MS);
        return ARGS_ERR;
      }
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
