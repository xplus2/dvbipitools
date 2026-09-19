/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "lib/cas/biss/biss.h"
#include "lib/cas/device_state_core.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/argutil.h"
#include "lib/helper/ioutil.h"
#include "config.h"
#include "version.h"

void dscr_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
}

static int set_buf(char *dst, size_t sz, const char *v, char *e, size_t n) {
  if (strlen(v) >= sz) {
    snprintf(e, n, "too long (max %zu)", sz - 1);
    return -1;
  }
  bufcpy(dst, sz, v);
  return 0;
}

static int set_pbkeylen(int *dst, const char *v, char *e, size_t n) {
  unsigned u;
  if (argutil_uint_range(v, 16, 32, &u) || (u != 16 && u != 24 && u != 32)) {
    snprintf(e, n, "invalid '%s' (16|24|32)", v);
    return -1;
  }
  *dst = (int)u;
  return 0;
}

static int apply_input(void *c, const char *v, char *e, size_t n) {
  if (dscr_cfg_set_input(c, v)) {
    snprintf(e, n, "invalid '%s' (uri)", v);
    return -1;
  }
  return 0;
}

static int apply_key(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->key_path, v, e, n);
}

static int apply_serial(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->serial, v, e, n);
}

static int apply_emm_file(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->emm_file, v, e, n);
}

static int apply_unicast_emm(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->unicast_emm_uri, v, e, n);
}

static int apply_insecure(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->insecure_tls, v, e, n);
}

static int apply_token_header(void *c, const char *v, char *e, size_t n) {
  const char *hdr;
  if (yamlcfg_set_str(&hdr, v, e, n)) return -1;
  if (dscr_cfg_token_header(c, hdr)) {
    snprintf(e, n, "invalid '%s' (no space or colon)", v);
    return -1;
  }
  return 0;
}

static int apply_output(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (cfg->n_out >= DIPIDESCRAMBLE_MAX_OUT) {
    snprintf(e, n, "too many output targets (max %d)", DIPIDESCRAMBLE_MAX_OUT);
    return -1;
  }
  if (dscr_cfg_add_out(cfg, v)) {
    snprintf(e, n, "invalid target '%s'", v);
    return -1;
  }
  return 0;
}

static int apply_format(void *c, const char *v, char *e, size_t n) {
  if (dscr_cfg_format(c, v)) {
    snprintf(e, n, "invalid '%s' (ts|mkv|mka)", v);
    return -1;
  }
  return 0;
}

static int apply_strip_lcevc(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->strip_lcevc, v, e, n);
}

static int apply_pmt_pid(void *c, const char *v, char *e, size_t n) {
  if (dscr_cfg_pmt(c, v)) {
    snprintf(e, n, "invalid '%s' (0x0010..0x1FFE, or all)", v);
    return -1;
  }
  return 0;
}

static int apply_iface(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->iface_in, v, e, n);
}

static int apply_verbose(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->verbose, v, e, n);
}

static int apply_daemonize(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->daemonize, v, e, n);
}

static int apply_color(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_color(&((config_t *)c)->color_mode, v, e, n);
}

static int apply_biss2_sw(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (biss_parse_hex16(v, cfg->biss2_sw)) {
    snprintf(e, n, "invalid '%s' (32 hex chars)", v);
    return -1;
  }
  cfg->biss2_sw_given = 1;
  return 0;
}

static int apply_biss2_esw(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (biss_parse_hex16(v, cfg->biss2_esw)) {
    snprintf(e, n, "invalid '%s' (32 hex chars)", v);
    return -1;
  }
  cfg->biss2_esw_given = 1;
  return 0;
}

static int apply_biss2_id(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (biss_parse_hex16(v, cfg->biss2_id)) {
    snprintf(e, n, "invalid '%s' (32 hex chars)", v);
    return -1;
  }
  cfg->biss2_id_given = 1;
  return 0;
}

static int apply_biss1_sw(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (biss1_parse_sw(v, cfg->biss1_sw)) {
    snprintf(e, n, "invalid '%s' (12 hex chars)", v);
    return -1;
  }
  cfg->biss1_sw_given = 1;
  return 0;
}

static int apply_biss2_ca_key(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->biss2_ca_key_path, v, e, n);
}

static int apply_ecm_profile(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (ecm_profile_parse(v, &cfg->ecm_profile) != 0 || ecm_profile_validate(&cfg->ecm_profile) != 0) {
    snprintf(e, n, "invalid '%s'", v);
    return -1;
  }
  return 0;
}

static int apply_metrics_sock(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->metrics_sock, v, e, n);
}

static int apply_metrics_id(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->metrics_id, v, e, n);
}

static int apply_metrics_interval(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->metrics_interval_s, v, 1, 86400, e, n);
}

static int apply_max_services(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->max_services, v, 1, DEVICE_MAX_SERVICES_CEILING, e, n);
}

static int apply_profile(void *c, const char *v, char *e, size_t n) {
  if (dscr_cfg_profile(c, v)) {
    snprintf(e, n, "invalid '%s' (simple|main)", v);
    return -1;
  }
  return 0;
}

static int apply_srt_passphrase_in(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->srt_passphrase_in, sizeof cfg->srt_passphrase_in, v, e, n);
}

static int apply_srt_pbkeylen_in(void *c, const char *v, char *e, size_t n) {
  return set_pbkeylen(&((config_t *)c)->srt_pbkeylen_in, v, e, n);
}

static int apply_srt_streamid_in(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->srt_streamid_in, sizeof cfg->srt_streamid_in, v, e, n);
}

static int apply_srt_packetfilter_in(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->srt_packetfilter_in, sizeof cfg->srt_packetfilter_in, v, e, n);
}

static int apply_srt_latency_in(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->srt_latency_in_ms, v, 1, 60000, e, n);
}

static int apply_srt_passphrase(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->srt_passphrase, sizeof cfg->srt_passphrase, v, e, n);
}

static int apply_srt_pbkeylen(void *c, const char *v, char *e, size_t n) {
  return set_pbkeylen(&((config_t *)c)->srt_pbkeylen, v, e, n);
}

static int apply_srt_streamid(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->srt_streamid, sizeof cfg->srt_streamid, v, e, n);
}

static int apply_srt_packetfilter(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  return set_buf(cfg->srt_packetfilter, sizeof cfg->srt_packetfilter, v, e, n);
}

static int apply_srt_latency(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->srt_latency_ms, v, 1, 60000, e, n);
}

static const yamlcfg_key_t keys[] = {
  {"input", apply_input, 0, 0},
  {"key", apply_key, 1, 0},
  {"serial", apply_serial, 0, 0},
  {"emm-file", apply_emm_file, 0, 0},
  {"unicast-emm", apply_unicast_emm, 0, 0},
  {"insecure", apply_insecure, 0, 0},
  {"token-header", apply_token_header, 0, 0},
  {"output", apply_output, 0, 1},
  {"format", apply_format, 0, 0},
  {"strip-lcevc", apply_strip_lcevc, 0, 0},
  {"pmt-pid", apply_pmt_pid, 0, 0},
  {"iface", apply_iface, 0, 0},
  {"verbose", apply_verbose, 0, 0},
  {"color", apply_color, 0, 0},
  {"daemonize", apply_daemonize, 0, 0},
  {"ecm-profile", apply_ecm_profile, 0, 0},
  {"max-services", apply_max_services, 0, 0},
  {"profile", apply_profile, 0, 0},
  {"biss1.sw", apply_biss1_sw, 0, 0},
  {"biss2.sw", apply_biss2_sw, 0, 0},
  {"biss2.esw", apply_biss2_esw, 0, 0},
  {"biss2.id", apply_biss2_id, 0, 0},
  {"biss2.ca-key", apply_biss2_ca_key, 1, 0},
  {"metrics.sock", apply_metrics_sock, 0, 0},
  {"metrics.id", apply_metrics_id, 0, 0},
  {"metrics.interval", apply_metrics_interval, 0, 0},
  {"srt.passphrase-in", apply_srt_passphrase_in, 0, 0},
  {"srt.pbkeylen-in", apply_srt_pbkeylen_in, 0, 0},
  {"srt.streamid-in", apply_srt_streamid_in, 0, 0},
  {"srt.packetfilter-in", apply_srt_packetfilter_in, 0, 0},
  {"srt.latency-in", apply_srt_latency_in, 0, 0},
  {"srt.passphrase", apply_srt_passphrase, 0, 0},
  {"srt.pbkeylen", apply_srt_pbkeylen, 0, 0},
  {"srt.streamid", apply_srt_streamid, 0, 0},
  {"srt.packetfilter", apply_srt_packetfilter, 0, 0},
  {"srt.latency", apply_srt_latency, 0, 0},
};

int dscr_cfg_load(config_t *cfg, const char *path, int strict) {
  yamlcfg_t y;
  int mode = strict ? YAMLCFG_STRICT : 0;
  int rc = yamlcfg_load(&y, TOOL_NAME, mode, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], cfg);
  return rc == YAMLCFG_ERROR ? -1 : 0;
}

static void warn_if(yamlcfg_t *y, int cond, const char *msg) {
  if (cond) yamlcfg_warn(y, "%s", msg);
}

static int count_kind(const config_t *cfg, out_kind_t k) {
  int n = 0;
  for (int i = 0; i < cfg->n_out; i++) if (cfg->out[i].kind == k) n++;
  return n;
}

int dscr_cfg_test(const char *path, int strict) {
  yamlcfg_t y;
  config_t cfg;
  int n_file;
  int n_rtmps;
  int n_rtmp;
  int n_srt;

  dscr_cfg_defaults(&cfg);
  if (yamlcfg_load(&y, TOOL_NAME, strict ? YAMLCFG_CHECK | YAMLCFG_STRICT : YAMLCFG_CHECK, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], &cfg) != YAMLCFG_LOADED) return -1;
  n_file = count_kind(&cfg, OUT_FILE);
  n_rtmps = count_kind(&cfg, OUT_RTMPS);
  n_rtmp = count_kind(&cfg, OUT_RTMP) + n_rtmps;
  n_srt = count_kind(&cfg, OUT_SRT);
  warn_if(&y, !cfg.have_input, "input not set (required unless given on the command line)");
  warn_if(&y, !cfg.n_out, "output not set (required unless given on the command line)");
  warn_if(&y, (cfg.format == FMT_MKV || cfg.format == FMT_MKA) && cfg.n_out && n_file != 1, "format mkv/mka requires exactly one file output target (plus optional rtmp(s) targets)");
  warn_if(&y, cfg.insecure_tls && !n_rtmps && !cfg.unicast_emm_uri, "insecure needs unicast-emm or an rtmps:// output target");
  warn_if(&y, cfg.strip_lcevc && cfg.format == FMT_TS && !n_rtmp, "strip-lcevc has no effect without format mkv/mka or an rtmp(s):// output target");
  warn_if(&y, cfg.biss2_sw_given && cfg.biss2_esw_given, "biss2.sw and biss2.esw are mutually exclusive");
  warn_if(&y, cfg.biss2_esw_given && !cfg.biss2_id_given, "biss2.esw requires biss2.id");
  warn_if(&y, cfg.biss2_id_given && !cfg.biss2_esw_given, "biss2.id requires biss2.esw");
  warn_if(&y, cfg.biss1_sw_given && (cfg.biss2_sw_given || cfg.biss2_esw_given), "biss1.sw is mutually exclusive with biss2.sw/biss2.esw");
  warn_if(&y, (cfg.metrics_sock || cfg.metrics_interval_s) && !cfg.metrics_id, "metrics.sock and metrics.interval require metrics.id");
  warn_if(&y, cfg.profile_given && cfg.have_input && cfg.input.kind != INPUT_RIST, "profile needs input rist://");
  warn_if(&y, cfg.srt_passphrase_in[0] && (strlen(cfg.srt_passphrase_in) < 10 || strlen(cfg.srt_passphrase_in) > 79), "srt.passphrase-in must be 10..79 characters");
  warn_if(&y, cfg.srt_pbkeylen_in && !cfg.srt_passphrase_in[0], "srt.pbkeylen-in requires srt.passphrase-in");
  warn_if(&y, cfg.have_input && cfg.input.kind != INPUT_SRT && (cfg.srt_passphrase_in[0] || cfg.srt_pbkeylen_in || cfg.srt_streamid_in[0] || cfg.srt_packetfilter_in[0] || cfg.srt_latency_in_ms), "srt.*-in settings need input srt://");
  warn_if(&y, cfg.srt_passphrase[0] && (strlen(cfg.srt_passphrase) < 10 || strlen(cfg.srt_passphrase) > 79), "srt.passphrase must be 10..79 characters");
  warn_if(&y, cfg.srt_pbkeylen && !cfg.srt_passphrase[0], "srt.pbkeylen requires srt.passphrase");
  warn_if(&y, cfg.n_out && !n_srt && (cfg.srt_passphrase[0] || cfg.srt_pbkeylen || cfg.srt_streamid[0] || cfg.srt_packetfilter[0] || cfg.srt_latency_ms), "srt.* settings need an srt:// output target");
  return yamlcfg_report(&y);
}
