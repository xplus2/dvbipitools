/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <unistd.h>

#include "priv.h"

static const char shortopts[] = "I:i:Jkl:L:j:c:f:n:dvh";

static const struct option longopts[] = {
  {"iface", required_argument, 0, 'I'},
  {"listen", required_argument, 0, 'l'},
  {"listen-tls", required_argument, 0, 'L'},
  {"tls-cert", required_argument, 0, OPT_TLS_CERT},
  {"tls-key", required_argument, 0, OPT_TLS_KEY},
  {"workers", required_argument, 0, 'j'},
  {"cpu-affinity", required_argument, 0, OPT_CPU_AFFINITY},
  {"max-clients", required_argument, 0, OPT_MAX_CLIENTS},
  {"max-channels", required_argument, 0, OPT_MAX_CHANNELS},
  {"idle-timeout", required_argument, 0, OPT_IDLE_TIMEOUT},
  {"capture-ring-size", required_argument, 0, OPT_CAPTURE_RING_SIZE},
  {"ts-startup-timeout", required_argument, 0, OPT_TS_STARTUP_TIMEOUT},
  {"sds-timeout", required_argument, 0, OPT_SDS_TIMEOUT},
  {"sds-refresh-interval", required_argument, 0, OPT_SDS_REFRESH_INTERVAL},
  {"segment-size", required_argument, 0, OPT_SEGMENT_SIZE},
  {"segment-count", required_argument, 0, OPT_SEGMENT_COUNT},
  {"hls-part-size", required_argument, 0, OPT_HLS_PART_SIZE},
  {"dash-part-size", required_argument, 0, OPT_DASH_PART_SIZE},
  {"dash-utc-url", required_argument, 0, OPT_DASH_UTC_URL},
  {"hls-seg-pool", required_argument, 0, OPT_HLS_SEG_POOL},
  {"metrics", required_argument, 0, OPT_METRICS},
  {"metrics-id", required_argument, 0, OPT_METRICS_ID},
  {"metrics-interval", required_argument, 0, OPT_METRICS_INTERVAL},
  {"metrics-inspect-ts", required_argument, 0, OPT_METRICS_INSPECT_TS},
  {"metrics-http", no_argument, 0, OPT_METRICS_HTTP},
  {"metrics-auth", required_argument, 0, OPT_METRICS_AUTH},
  {"format", required_argument, 0, 'f'},
  {"no-url-rtp", no_argument, 0, OPT_NO_URL_RTP},
  {"no-url-udp", no_argument, 0, OPT_NO_URL_UDP},
  {"no-url-srt", no_argument, 0, OPT_NO_URL_SRT},
  {"no-pid-filters", no_argument, 0, OPT_NO_PID_FILTERS},
  {"no-lcevc", no_argument, 0, OPT_NO_LCEVC},
  {"no-http2", no_argument, 0, OPT_NO_HTTP2},
  {"no-http3", no_argument, 0, OPT_NO_HTTP3},
  {"h3-altsvc-port", required_argument, 0, OPT_H3_ALTSVC_PORT},
  {"h3-max-streams", required_argument, 0, OPT_H3_MAX_STREAMS},
  {"h3-max-conns", required_argument, 0, OPT_H3_MAX_CONNS},
  {"h3-idle-timeout", required_argument, 0, OPT_H3_IDLE_TIMEOUT},
  {"h3-retry", required_argument, 0, OPT_H3_RETRY},
  {"h3-max-udp-payload", required_argument, 0, OPT_H3_MAX_UDP_PAYLOAD},
  {"h3-window", required_argument, 0, OPT_H3_WINDOW},
  {"h3-cc", required_argument, 0, OPT_H3_CC},
  {"no-fcc", no_argument, 0, OPT_NO_FCC},
  {"no-ret", no_argument, 0, OPT_NO_RET},
  {"al-fec", required_argument, 0, OPT_AL_FEC},
  {"no-al-fec", no_argument, 0, OPT_NO_AL_FEC},
  {"no-status", no_argument, 0, OPT_NO_STATUS},
  {"status-tpl", required_argument, 0, OPT_STATUS_TPL},
  {"auth", required_argument, 0, OPT_AUTH},
  {"cors-origin", required_argument, 0, OPT_CORS_ORIGIN},
  {"ssdp-ttl", required_argument, 0, OPT_SSDP_TTL},
  {"ssdp-iface", required_argument, 0, OPT_SSDP_IFACE},
  {"ssdp-interval", required_argument, 0, OPT_SSDP_INTERVAL},
  {"ssdp-max-age", required_argument, 0, OPT_SSDP_MAX_AGE},
  {"enable-dlna", no_argument, 0, OPT_ENABLE_DLNA},
  {"dlna-host", required_argument, 0, OPT_DLNA_HOST},
  {"dlna-name", required_argument, 0, OPT_DLNA_NAME},
  {"dlna-keep-multicast", no_argument, 0, OPT_DLNA_KEEP_MULTICAST},
  {"media-type", required_argument, 0, OPT_MEDIA_TYPE},
  {"input", required_argument, 0, 'i'},
  {"join-all", no_argument, 0, 'J'},
  {"insecure", no_argument, 0, 'k'},
  {"name", required_argument, 0, 'n'},
  {"daemonize", no_argument, 0, 'd'},
  {"verbose", no_argument, 0, 'v'},
  {"color", required_argument, 0, OPT_COLOR},
  {"config", required_argument, 0, 'c'},
  {"config-strict", no_argument, 0, OPT_CONFIG_STRICT},
  {"configtest", no_argument, 0, OPT_CONFIGTEST},
  {"help", no_argument, 0, 'h'},
  {0, 0, 0, 0}};


static args_status_t prescan(int argc, char **argv, const char **cfg_path, int *configtest, int *strict) {
  int c;
  optind = 1;
  opterr = 0;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    if (c == 'c') *cfg_path = optarg;
    if (c == OPT_CONFIGTEST) *configtest = 1;
    if (c == OPT_CONFIG_STRICT) *strict = 1;
    if (c == 'h') {
      dixy_print_help();
      opterr = 1;
      return ARGS_HELP;
    }
  }
  opterr = 1;
  return ARGS_OK;
}

static args_status_t dispatch(dixy_opt_t *p, int c) {
  static args_status_t (*const groups[])(dixy_opt_t *, int) = {dixy_opt_server, dixy_opt_dlna, dixy_opt_stream, dixy_opt_general, dixy_opt_http};

  for (size_t i = 0; i < sizeof groups / sizeof groups[0]; i++) {
    args_status_t st = groups[i](p, c);
    if (st != OPT_UNHANDLED) return st;
  }
  args_free(p->cfg);
  return ARGS_ERR; /* getopt already reported */
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  const char *cfg_path = NULL;
  int configtest = 0;
  int strict = 0;
  dixy_opt_t p = {.cfg = cfg};
  args_status_t pst;
  int c;

  if (argc == 1 && access(DEFAULT_CONFIG_PATH, R_OK))
    return ARGS_NOARGS;

  pst = prescan(argc, argv, &cfg_path, &configtest, &strict);
  if (pst != ARGS_OK) return pst;
  if (configtest) return dixy_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;

  dixy_cfg_defaults(cfg);
  if (dixy_cfg_load(cfg, cfg_path, strict)) {
    args_free(cfg);
    return ARGS_ERR;
  }

  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    args_status_t st;
    if (c == 'c' || c == OPT_CONFIG_STRICT || c == OPT_CONFIGTEST) continue;
    if (c == 'h') {
      dixy_print_help();
      return ARGS_HELP;
    }
    st = dispatch(&p, c);
    if (st != ARGS_OK) return st;
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    args_free(cfg);
    return ARGS_ERR;
  }
  return dixy_cli_check(cfg);
}
