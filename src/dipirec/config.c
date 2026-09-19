/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "lib/config/yamlcfg.h"
#include "lib/helper/argutil.h"
#include "lib/helper/ioutil.h"
#include "lib/mux/fec2022.h"
#include "config.h"
#include "filter/ts.h"
#include "version.h"

void rec_cfg_defaults(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->audio_all = 1;
  cfg->subs = SUB_KEEP;
  cfg->sub_lead_ms = 1000;
  cfg->ret.mc_enabled = 1;
  cfg->ret.rtx_pt = 99;
  cfg->ret.wait_ms = 200;
  cfg->strip_mask = STRIP_DEFAULT;
}

static int set_buf(char *dst, size_t sz, const char *v, char *e, size_t n) {
  if (strlen(v) >= sz) {
    snprintf(e, n, "too long (max %zu)", sz - 1);
    return -1;
  }
  bufcpy(dst, sz, v);
  return 0;
}

static int set_byte(unsigned char *dst, const char *v, unsigned max, char *e, size_t n) {
  unsigned u;
  if (yamlcfg_set_uint(&u, v, 0, max, e, n)) return -1;
  *dst = (unsigned char)u;
  return 0;
}

static int apply_in(void *c, const char *v, char *e, size_t n) {
  if (rec_cfg_set_in(c, v)) {
    snprintf(e, n, "invalid '%s' (uri)", v);
    return -1;
  }
  return 0;
}

static int apply_out(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (cfg->n_out >= DIPIREC_MAX_OUT) {
    snprintf(e, n, "too many out targets (max %d)", DIPIREC_MAX_OUT);
    return -1;
  }
  if (rec_cfg_add_out(cfg, v)) {
    snprintf(e, n, "invalid target '%s'", v);
    return -1;
  }
  return 0;
}

static int apply_audio(void *c, const char *v, char *e, size_t n) {
  if (rec_cfg_audio(c, v)) {
    snprintf(e, n, "invalid '%s' (1..N or all)", v);
    return -1;
  }
  return 0;
}

static int apply_format(void *c, const char *v, char *e, size_t n) {
  if (rec_cfg_format(c, v)) {
    snprintf(e, n, "invalid '%s' (raw|ts|mkv|mka|mp4|m4a)", v);
    return -1;
  }
  return 0;
}

static int apply_pmt_pid(void *c, const char *v, char *e, size_t n) {
  if (rec_cfg_pmt(c, v)) {
    snprintf(e, n, "invalid '%s' (0x0010..0x1FFE, or all)", v);
    return -1;
  }
  return 0;
}

static int apply_subtitles(void *c, const char *v, char *e, size_t n) {
  if (rec_cfg_subs(c, v)) {
    snprintf(e, n, "invalid '%s' (strip|keep|srt)", v);
    return -1;
  }
  return 0;
}

static int apply_time(void *c, const char *v, char *e, size_t n) {
  if (rec_cfg_time(c, v)) {
    snprintf(e, n, "invalid '%s' (duration such as 90, 5m30s, 01:20:03)", v);
    return -1;
  }
  return 0;
}

static int apply_iface(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->iface_in, v, e, n);
}

static int apply_out_iface(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_str(&((config_t *)c)->iface_out, v, e, n);
}

static int apply_ttl(void *c, const char *v, char *e, size_t n) {
  unsigned u;
  if (yamlcfg_set_uint(&u, v, 0, 255, e, n)) return -1;
  ((config_t *)c)->out_ttl = (int)u;
  return 0;
}

static int apply_al_fec(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (fec2022_parse_ld(v, &cfg->al_fec_l, &cfg->al_fec_d)) {
    snprintf(e, n, "invalid '%s' (want L:D, L*D<=400, L<=40)", v);
    return -1;
  }
  return 0;
}

static int apply_al_fec_port(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->al_fec_port, v, 1, 65535, e, n);
}

static int apply_profile(void *c, const char *v, char *e, size_t n) {
  if (rec_cfg_profile(c, v, 0)) {
    snprintf(e, n, "invalid '%s' (simple|main)", v);
    return -1;
  }
  return 0;
}

static int apply_secret(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (set_buf(cfg->rist_secret, sizeof cfg->rist_secret, v, e, n)) return -1;
  cfg->fl.have_secret = 1;
  return 0;
}

static int apply_cname(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (set_buf(cfg->rist_cname, sizeof cfg->rist_cname, v, e, n)) return -1;
  cfg->fl.have_cname = 1;
  return 0;
}

static int apply_buffer(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_uint(&cfg->rist_buffer_ms, v, 1, UINT_MAX, e, n)) return -1;
  cfg->fl.have_buffer = 1;
  return 0;
}

static int apply_profile_in(void *c, const char *v, char *e, size_t n) {
  if (rec_cfg_profile(c, v, 1)) {
    snprintf(e, n, "invalid '%s' (simple|main)", v);
    return -1;
  }
  return 0;
}

static int apply_insecure(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->insecure_tls, v, e, n);
}

static int apply_verbose(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->verbose, v, e, n);
}

static int apply_sub_lead(void *c, const char *v, char *e, size_t n) {
  unsigned u;
  if (yamlcfg_set_uint(&u, v, 0, 10000, e, n)) return -1;
  ((config_t *)c)->sub_lead_ms = (long)u;
  return 0;
}

static int apply_color(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_color(&((config_t *)c)->color_mode, v, e, n);
}

static int apply_pace(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_bool(&((config_t *)c)->pace, v, e, n);
}

static int apply_strip(void *c, const char *v, char *e, size_t n) {
  if (rec_cfg_strip(c, v)) {
    snprintf(e, n, "invalid '%s' (comma list of NUL,NIT,AIT,EIT,CAT,ECM,EMM,RST,TDT,TOT,INT,LCEVC, or none)", v);
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

static int apply_ret_addr(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (yamlcfg_set_addrport(&cfg->ret.family, cfg->ret.addr, sizeof cfg->ret.addr, &cfg->ret.port, v, e, n)) return -1;
  cfg->ret.enabled = 1;
  return 0;
}

static int apply_ret_no_mc(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  int off;
  if (yamlcfg_set_bool(&off, v, e, n)) return -1;
  cfg->ret.mc_enabled = !off;
  return 0;
}

static int apply_ret_mc_port(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->ret.mc_port, v, 1, 65535, e, n);
}

static int apply_ret_pt(void *c, const char *v, char *e, size_t n) {
  return set_byte(&((config_t *)c)->ret.rtx_pt, v, 127, e, n);
}

static int apply_ret_wait(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->ret.wait_ms, v, 1, UINT_MAX, e, n);
}

static int apply_srt_passphrase_in(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (set_buf(cfg->srt_passphrase_in, sizeof cfg->srt_passphrase_in, v, e, n)) return -1;
  return 0;
}

static int apply_srt_pbkeylen_in(void *c, const char *v, char *e, size_t n) {
  unsigned u;
  if (argutil_uint_range(v, 16, 32, &u) || (u != 16 && u != 24 && u != 32)) {
    snprintf(e, n, "invalid '%s' (16|24|32)", v);
    return -1;
  }
  ((config_t *)c)->srt_pbkeylen_in = (int)u;
  return 0;
}

static int apply_srt_streamid_in(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (set_buf(cfg->srt_streamid_in, sizeof cfg->srt_streamid_in, v, e, n)) return -1;
  return 0;
}

static int apply_srt_packetfilter_in(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (set_buf(cfg->srt_packetfilter_in, sizeof cfg->srt_packetfilter_in, v, e, n)) return -1;
  return 0;
}

static int apply_srt_latency_in(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->srt_latency_in_ms, v, 1, 60000, e, n);
}

static int apply_srt_passphrase(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (set_buf(cfg->srt_passphrase, sizeof cfg->srt_passphrase, v, e, n)) return -1;
  return 0;
}

static int apply_srt_pbkeylen(void *c, const char *v, char *e, size_t n) {
  unsigned u;
  if (argutil_uint_range(v, 16, 32, &u) || (u != 16 && u != 24 && u != 32)) {
    snprintf(e, n, "invalid '%s' (16|24|32)", v);
    return -1;
  }
  ((config_t *)c)->srt_pbkeylen = (int)u;
  return 0;
}

static int apply_srt_streamid(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (set_buf(cfg->srt_streamid, sizeof cfg->srt_streamid, v, e, n)) return -1;
  return 0;
}

static int apply_srt_packetfilter(void *c, const char *v, char *e, size_t n) {
  config_t *cfg = c;
  if (set_buf(cfg->srt_packetfilter, sizeof cfg->srt_packetfilter, v, e, n)) return -1;
  return 0;
}

static int apply_srt_latency(void *c, const char *v, char *e, size_t n) {
  return yamlcfg_set_uint(&((config_t *)c)->srt_latency_ms, v, 1, 60000, e, n);
}

static const yamlcfg_key_t keys[] = {
  {"in", apply_in, 0, 0},
  {"out", apply_out, 0, 1},
  {"audio", apply_audio, 0, 0},
  {"format", apply_format, 0, 0},
  {"pmt-pid", apply_pmt_pid, 0, 0},
  {"subtitles", apply_subtitles, 0, 0},
  {"time", apply_time, 0, 0},
  {"iface", apply_iface, 0, 0},
  {"out-iface", apply_out_iface, 0, 0},
  {"ttl", apply_ttl, 0, 0},
  {"al-fec", apply_al_fec, 0, 0},
  {"al-fec-port", apply_al_fec_port, 0, 0},
  {"profile", apply_profile, 0, 0},
  {"secret", apply_secret, 0, 0},
  {"cname", apply_cname, 0, 0},
  {"buffer", apply_buffer, 0, 0},
  {"profile-in", apply_profile_in, 0, 0},
  {"insecure", apply_insecure, 0, 0},
  {"verbose", apply_verbose, 0, 0},
  {"sub-lead", apply_sub_lead, 0, 0},
  {"color", apply_color, 0, 0},
  {"pace", apply_pace, 0, 0},
  {"strip", apply_strip, 0, 0},
  {"metrics.sock", apply_metrics_sock, 0, 0},
  {"metrics.id", apply_metrics_id, 0, 0},
  {"metrics.interval", apply_metrics_interval, 0, 0},
  {"ret.addr", apply_ret_addr, 0, 0},
  {"ret.no-mc", apply_ret_no_mc, 0, 0},
  {"ret.mc-port", apply_ret_mc_port, 0, 0},
  {"ret.pt", apply_ret_pt, 0, 0},
  {"ret.wait", apply_ret_wait, 0, 0},
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

int rec_cfg_load(config_t *cfg, const char *path, int strict) {
  yamlcfg_t y;
  return yamlcfg_load(&y, TOOL_NAME, strict ? YAMLCFG_STRICT : 0, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], cfg) == YAMLCFG_ERROR ? -1 : 0;
}

static void warn_if(yamlcfg_t *y, int cond, const char *msg) {
  if (cond) yamlcfg_warn(y, "%s", msg);
}

static int count_kind(const config_t *cfg, out_kind_t k) {
  int n = 0;
  for (int i = 0; i < cfg->n_out; i++) if (cfg->out[i].kind == k) n++;
  return n;
}

int rec_cfg_test(const char *path, int strict) {
  yamlcfg_t y;
  config_t cfg;
  int n_rist = 0;
  int n_file = 0;
  int n_rtmp = 0;
  int has_srt = 0;
  int container;
  const args_flags_t *fl = &cfg.fl;

  rec_cfg_defaults(&cfg);
  if (yamlcfg_load(&y, TOOL_NAME, strict ? YAMLCFG_CHECK | YAMLCFG_STRICT : YAMLCFG_CHECK, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], &cfg) != YAMLCFG_LOADED) return -1;
  n_rist = count_kind(&cfg, OUT_RIST);
  n_file = count_kind(&cfg, OUT_FILE);
  n_rtmp = count_kind(&cfg, OUT_RTMP) + count_kind(&cfg, OUT_RTMPS);
  has_srt = count_kind(&cfg, OUT_SRT);
  container = fl->have_format && (cfg.format == FMT_MKV || cfg.format == FMT_MKA || cfg.format == FMT_MP4 || cfg.format == FMT_M4A);

  warn_if(&y, !fl->have_in, "in not set (required unless given on the command line)");
  warn_if(&y, !cfg.n_out, "out not set (required unless given on the command line)");
  warn_if(&y, n_rist > 1, "at most one rist:// out target");
  warn_if(&y, fl->have_in && cfg.source.kind == URI_RIST && n_rist, "in rist:// and out rist:// cannot combine");
  warn_if(&y, cfg.ret.enabled && fl->have_in && cfg.source.kind != URI_RTP, "ret.addr requires in rtp://");
  warn_if(&y, cfg.ret.enabled && cfg.ret.mc_enabled && fl->have_in && cfg.ret.family != cfg.source.family, "ret.addr family must match in's, or set ret.no-mc");
  warn_if(&y, cfg.pace && fl->have_in && cfg.source.kind != URI_FILE, "pace requires in - or a file path");
  warn_if(&y, container && cfg.n_out && (n_file != 1 || cfg.n_out - n_rtmp != 1), "format mkv/mka/mp4/m4a requires exactly one file out target (plus optional rtmp(s) targets)");
  warn_if(&y, fl->have_format && cfg.format == FMT_RAW && n_rtmp, "format raw is incompatible with rtmp(s) out targets");
  warn_if(&y, cfg.subs == SUB_SRT && fl->have_format && !container, "subtitles srt requires format mkv, mka, mp4 or m4a");
  warn_if(&y, fl->have_secret && n_rist && cfg.rist_profile != RIST_PROF_MAIN, "secret requires profile main");
  warn_if(&y, cfg.al_fec_l && !cfg.al_fec_port, "al-fec requires al-fec-port");
  warn_if(&y, (cfg.metrics_sock || cfg.metrics_interval_s) && !cfg.metrics_id, "metrics.sock and metrics.interval require metrics.id");
  warn_if(&y, cfg.srt_passphrase_in[0] && (strlen(cfg.srt_passphrase_in) < 10 || strlen(cfg.srt_passphrase_in) > 79), "srt.passphrase-in must be 10..79 characters");
  warn_if(&y, cfg.srt_pbkeylen_in && !cfg.srt_passphrase_in[0], "srt.pbkeylen-in requires srt.passphrase-in");
  warn_if(&y, cfg.srt_passphrase[0] && (strlen(cfg.srt_passphrase) < 10 || strlen(cfg.srt_passphrase) > 79), "srt.passphrase must be 10..79 characters");
  warn_if(&y, cfg.srt_pbkeylen && !cfg.srt_passphrase[0], "srt.pbkeylen requires srt.passphrase");
  warn_if(&y, !has_srt && (cfg.srt_passphrase[0] || cfg.srt_streamid[0] || cfg.srt_packetfilter[0] || cfg.srt_latency_ms) && cfg.n_out, "srt.* settings need an srt:// out target");
  return yamlcfg_report(&y);
}
