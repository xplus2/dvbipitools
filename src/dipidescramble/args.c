/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/cas/biss/biss.h"
#include "lib/cas/device_state_core.h"
#include "lib/helper/describe.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/uriparse.h"
#include "args.h"
#include "version.h"

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

/* [@]<addr>:<port> or [@][<addr6>]:<port>, multicast literal required */
static int mcast_group_parse(const char *s, int *family, char *addr_out, size_t addr_out_sz, unsigned *port_out) {
  if (*s == '@') s++;
  return uriparse_mcast_addrport(s, family, addr_out, addr_out_sz, port_out);
}

static int fmt_from_name(const char *s, out_fmt_t *f) {
  static const enum_map_t map[] = {{"ts", FMT_TS}, {"mkv", FMT_MKV}, {"mka", FMT_MKA}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], s, &v)) return -1;
  *f = (out_fmt_t)v;
  return 0;
}

/* decimal or 0x-hex, PMT pid range 0x0010..0x1FFE */
static int pid_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 0);
  if (*end != '\0' || v < 0x0010 || v > 0x1FFE) return -1;
  *out = (unsigned)v;
  return 0;
}

static int parse_pmt_sel(const char *s, config_t *cfg) {
  if (strcmp(s, "all") == 0) {
    cfg->pmt_sel = PMT_SEL_ALL;
    return 0;
  }
  if (pid_parse(s, &cfg->pmt_pid)) return -1;
  cfg->pmt_sel = PMT_SEL_PID;
  return 0;
}

static int input_parse(const char *uri, input_t *s) {
  memset(s, 0, sizeof *s);
  if (strcmp(uri, "-") == 0) {
    s->kind = INPUT_STDIN;
    return 0;
  }
  if (strncmp(uri, "rtp://", 6) == 0) {
    s->kind = INPUT_RTP;
    return mcast_group_parse(uri + 6, &s->family, s->group, sizeof s->group, &s->port);
  }
  if (strncmp(uri, "udp://", 6) == 0) {
    s->kind = INPUT_UDP;
    return mcast_group_parse(uri + 6, &s->family, s->group, sizeof s->group, &s->port);
  }
  if (strncmp(uri, "rist://", 7) == 0) {
    if (uri[7] != '@') return -1; /* rist:// as input always listens */
    if (strlen(uri) >= sizeof s->rist_uri) return -1;
    s->kind = INPUT_RIST;
    bufcpy(s->rist_uri, sizeof s->rist_uri, uri);
    return 0;
  }
  if (strncmp(uri, "srt://", 6) == 0) {
    const char *rest = uri + 6;
    int listen = *rest == '@';
    if (listen) rest++;
    if (argutil_addrport_parse(rest, &s->srt_family, s->srt_host, sizeof s->srt_host, &s->srt_port)) return -1;
    s->kind = INPUT_SRT;
    s->srt_listen = listen;
    return 0;
  }
  return -1;
}

void input_describe(const input_t *s, char *buf, size_t n) {
  switch (s->kind) {
    case INPUT_RTP:
      describe_mcast_uri(buf, n, "rtp", s->family, s->group, s->port);
      break;
    case INPUT_UDP:
      describe_mcast_uri(buf, n, "udp", s->family, s->group, s->port);
      break;
    case INPUT_STDIN:
      bufcpy(buf, n, "-");
      break;
    case INPUT_RIST:
      bufcpy(buf, n, s->rist_uri);
      break;
    case INPUT_SRT:
      describe_srt_uri(buf, n, s->srt_family, s->srt_listen, s->srt_host, s->srt_port);
      break;
  }
}

static int parse_out_uri(const char *uri, out_target_t *o) {
  int r;
  memset(o, 0, sizeof *o);
  if (strncmp(uri, "srt://", 6) == 0) {
    if (uri[6] == '@') return -1; /* srt:// output always calls out, no listener mode */
    if (argutil_addrport_parse(uri + 6, &o->srt_family, o->srt_host, sizeof o->srt_host, &o->srt_port)) return -1;
    o->kind = OUT_SRT;
    return 0;
  }
  r = uriparse_rtmp_or_file(uri, o->rtmp_url, sizeof o->rtmp_url, o->file_path, sizeof o->file_path);
  if (r < 0) return -1;
  o->kind = r == 2 ? OUT_RTMPS : r == 1 ? OUT_RTMP : OUT_FILE;
  return 0;
}

void out_describe(const out_target_t *o, char *buf, size_t n) {
  switch (o->kind) {
    case OUT_RTMP:
    case OUT_RTMPS:
      bufcpy(buf, n, o->rtmp_url);
      break;
    case OUT_FILE:
      bufcpy(buf, n, strcmp(o->file_path, "-") == 0 ? "- (stdout)" : o->file_path);
      break;
    case OUT_SRT:
      if (o->srt_family == AF_INET6) snprintf(buf, n, "srt://[%s]:%u", o->srt_host, o->srt_port);
      else                           snprintf(buf, n, "srt://%s:%u", o->srt_host, o->srt_port);
      break;
  }
}

static void print_help(void) {
  printf(
    "usage: %s -i <uri> -k <keyfile> -s <serial> -e <emmfile> -o <output> [options]\n\n"
    "standalone CAS validation client: descrambles a DVB-CSA1/CSA2/CISSA/BISS transport stream,\n"
    "given the device's RSA private key or a BISS session word. The CAS scheme is auto-detected from\n"
    "the stream; -k/-s/-e or --biss-* are only required once the stream turns out to need them.\n\n"
    "options:\n"
    "  -i, --input <uri>          udp://, rtp://, rist://@host:port[?query] (single peer)\n"
    "                             srt://[@]host:port (single peer), or \"-\" for stdin (required)\n"
    "  -k, --key <path>           device RSA private key, PEM (required for ECM/EMM-driven CAS)\n"
    "  -s, --serial <id>          this device's serial, matched against EMM-U addressing (required for ECM/EMM-driven CAS)\n"
    "  -e, --emm-file <path>      EMM cache: loaded on startup, rewritten on update (required for ECM/EMM-driven CAS)\n"
    "  -u, --unicast-emm <uri>    unicast EMM pull endpoint, auth token as URI userinfo\n"
    "                             (e.g. https://<token>@<host>:<port>/device/<serial>/emm)\n"
    "      --insecure             skip TLS verification for -u/--unicast-emm and -o rtmps://\n"
    "      --token-header <name>  HTTP header carrying the token for -u/--unicast-emm (default X-Device-Token)\n"
    "      --biss2-sw <hex32>     BISS2 Mode 1: 32 hex char Session Word\n"
    "      --biss2-esw <hex32>    BISS2 Mode E: 32 hex char Encrypted Session Word (needs --biss2-id)\n"
    "      --biss2-id <hex32>     BISS2 Mode E: 32 hex char receiver ID for --biss2-esw\n"
    "      --biss1-sw <hex12>     legacy BISS1 Mode 1: 12 hex char Session Word\n"
    "      --biss2-ca-key <path>  BISS Mode CA: receiver RSA private key, PEM\n"
    "      --ecm-profile <spec>   ecm_profile template, comma key=value (see README)\n"
    "  -o, --output <target>      descrambled output, repeatable: file, \"-\" for stdout, rtmp(s)://,\n"
    "                             or srt://host:port, rtmp(s)://<host>[:port]/<app>/<key>\n"
    "  -f, --format <fmt>         ts|mkv|mka output container (default ts; raw ts and rtmp(s) targets\n"
    "                             may mix, mkv/mka needs exactly one plain file target)\n"
    "      --strip-lcevc          drop inline LCEVC (SEI/NAL) from mkv/mka/rtmp output\n"
    "  -p, --pmt-pid <pid|all>    MPTS source only: pin one PMT pid, or descramble every program\n"
    "                             (\"all\"; rejected with -f mkv). ignored on an SPTS\n"
    "                             source. omitted on an MPTS source: lists programs\n"
    "  -I, --iface <iface>        incoming multicast interface name\n"
    "  -v, --verbose              periodic stats + BK/SK/CW update lines on stderr\n"
    "      --color <when>         auto|always|never (default auto)\n"
    "      --metrics <path>       socket for metrics (default: /run/dvbipitools/metrics.sock)\n"
    "      --metrics-id <name>    stable instance id. metrics are disabled unless set\n"
    "      --metrics-interval <s> snapshot interval in seconds (default: 5)\n"
    "      --max-services <n>     max distinct EMM-G service_ids cached (default: 32, max: 256)\n"
    "      --profile <p>          simple|main; -i rist:// only (default: simple)\n"
    "      --srt-passphrase-in <p>passphrase for -i srt://, 10..79 chars\n"
    "      --srt-pbkeylen-in <n>  AES key length for --srt-passphrase-in: 16|24|32 (default 16)\n"
    "      --srt-streamid-in <id> SRTO_STREAMID for -i srt://\n"
    "      --srt-packetfilter-in <cfg>SRTO_PACKETFILTER for -i srt://, e.g. fec,cols:10,rows:5\n"
    "      --srt-latency-in <ms>  SRTO_LATENCY (ms) for -i srt://\n"
    "      --srt-passphrase <pw>  passphrase for every -o srt:// target, 10..79 chars\n"
    "      --srt-pbkeylen <n>     AES key length for --srt-passphrase: 16|24|32 (default 16)\n"
    "      --srt-streamid <id>    SRTO_STREAMID for every -o srt:// target\n"
    "      --srt-packetfilter <cfg>SRTO_PACKETFILTER for every -o srt:// target\n"
    "      --srt-latency <ms>     SRTO_LATENCY (ms) for every -o srt:// target\n"
    "  -d, --daemonize            fork to background after startup, detach from terminal\n"
    "  -h, --help                 this help\n\n"
    "examples:\n"
    "  %s -i rtp://@239.0.0.1:1975 -k device.key -s e2e-01 -e emm.cache -o out.ts -v\n"
    "  %s -i rtp://@239.0.0.1:1975 --biss2-sw 00112233445566778899aabbccddeeff -o out.ts\n"
    "  %s -i rtp://@239.0.0.1:1975 --biss2-sw 00112233445566778899aabbccddeeff -o rtmp://live.example.com/app/key\n",
    TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"input", required_argument, 0, 'i'},
      {"key", required_argument, 0, 'k'},
      {"serial", required_argument, 0, 's'},
      {"emm-file", required_argument, 0, 'e'},
      {"unicast-emm", required_argument, 0, 'u'},
      {"insecure", no_argument, 0, 1003},
      {"token-header", required_argument, 0, 1010},
      {"output", required_argument, 0, 'o'},
      {"format", required_argument, 0, 'f'},
      {"pmt-pid", required_argument, 0, 'p'},
      {"iface", required_argument, 0, 'I'},
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1000},
      {"biss2-sw", required_argument, 0, 1004},
      {"biss2-esw", required_argument, 0, 1005},
      {"biss2-id", required_argument, 0, 1006},
      {"biss1-sw", required_argument, 0, 1007},
      {"biss2-ca-key", required_argument, 0, 1008},
      {"ecm-profile", required_argument, 0, 1009},
      {"metrics", required_argument, 0, 1011},
      {"metrics-id", required_argument, 0, 1012},
      {"metrics-interval", required_argument, 0, 1013},
      {"max-services", required_argument, 0, 1014},
      {"profile", required_argument, 0, 1015},
      {"srt-passphrase-in", required_argument, 0, 1016},
      {"srt-pbkeylen-in", required_argument, 0, 1017},
      {"srt-streamid-in", required_argument, 0, 1018},
      {"srt-packetfilter-in", required_argument, 0, 1019},
      {"srt-latency-in", required_argument, 0, 1020},
      {"srt-passphrase", required_argument, 0, 1021},
      {"srt-pbkeylen", required_argument, 0, 1022},
      {"srt-streamid", required_argument, 0, 1023},
      {"srt-packetfilter", required_argument, 0, 1024},
      {"srt-latency", required_argument, 0, 1025},
      {"strip-lcevc", no_argument, 0, 1026},
      {"daemonize", no_argument, 0, 'd'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_input = 0, have_biss_id = 0, profile_given = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  optind = 1;
  while ((c = getopt_long(argc, argv, "i:k:s:e:u:o:f:p:I:vdh", longopts, NULL)) != -1) {
    switch (c) {
      case 'i':
        if (input_parse(optarg, &cfg->input)) {
          argerr("invalid -i input: %s", optarg);
          return ARGS_ERR;
        }
        have_input = 1;
        break;
      case 'k':
        cfg->key_path = optarg;
        break;
      case 's':
        cfg->serial = optarg;
        break;
      case 'e':
        cfg->emm_file = optarg;
        break;
      case 'u':
        cfg->unicast_emm_uri = optarg;
        break;
      case 1003:
        cfg->insecure_tls = 1;
        break;
      case 1026:
        cfg->strip_lcevc = 1;
        break;
      case 1010:
        if (!optarg[0] || strpbrk(optarg, ":\r\n ")) {
          argerr("invalid --token-header: %s", optarg);
          return ARGS_ERR;
        }
        cfg->unicast_emm_token_header = optarg;
        break;
      case 'o':
        if (cfg->n_out >= DIPIDESCRAMBLE_MAX_OUT) {
          argerr("too many -o targets (max %d)", DIPIDESCRAMBLE_MAX_OUT);
          return ARGS_ERR;
        }
        if (parse_out_uri(optarg, &cfg->out[cfg->n_out])) {
          argerr("invalid -o target: %s", optarg);
          return ARGS_ERR;
        }
        cfg->n_out++;
        break;
      case 'f':
        if (fmt_from_name(optarg, &cfg->format)) {
          argerr("invalid -f format: %s (ts|mkv|mka)", optarg);
          return ARGS_ERR;
        }
        break;
      case 'p':
        if (parse_pmt_sel(optarg, cfg)) {
          argerr("invalid -p pmt-pid: %s (0x0010..0x1FFE, or \"all\")", optarg);
          return ARGS_ERR;
        }
        break;
      case 'I':
        cfg->iface_in = optarg;
        break;
      case 'v':
        cfg->verbose = 1;
        break;
      case 'd':
        cfg->daemonize = 1;
        break;
      case 1000:
        {
          log_color_t v;
          if (log_color_from_string(optarg, &v)) {
            argerr("invalid --color: %s (auto|always|never)", optarg);
            return ARGS_ERR;
          }
          cfg->color_mode = v;
        }
        break;
      case 1004:
        if (biss_parse_hex16(optarg, cfg->biss2_sw)) {
          argerr("invalid --biss2-sw: %s (32 hex chars)", optarg);
          return ARGS_ERR;
        }
        cfg->biss2_sw_given = 1;
        break;
      case 1005:
        if (biss_parse_hex16(optarg, cfg->biss2_esw)) {
          argerr("invalid --biss2-esw: %s (32 hex chars)", optarg);
          return ARGS_ERR;
        }
        cfg->biss2_esw_given = 1;
        break;
      case 1006:
        if (biss_parse_hex16(optarg, cfg->biss2_id)) {
          argerr("invalid --biss2-id: %s (32 hex chars)", optarg);
          return ARGS_ERR;
        }
        have_biss_id = 1;
        break;
      case 1007:
        if (biss1_parse_sw(optarg, cfg->biss1_sw)) {
          argerr("invalid --biss1-sw: %s (12 hex chars)", optarg);
          return ARGS_ERR;
        }
        cfg->biss1_sw_given = 1;
        break;
      case 1008:
        cfg->biss2_ca_key_path = optarg;
        break;
      case 1009:
        if (ecm_profile_parse(optarg, &cfg->ecm_profile) != 0 || ecm_profile_validate(&cfg->ecm_profile) != 0) {
          argerr("invalid --ecm-profile: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 1011:
        cfg->metrics_sock = optarg;
        break;
      case 1012:
        cfg->metrics_id = optarg;
        break;
      case 1013:
        if (argutil_metrics_interval_opt(TOOL_NAME, optarg, &cfg->metrics_interval_s)) return ARGS_ERR;
        break;
      case 1014: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, DEVICE_MAX_SERVICES_CEILING, &v)) {
          argerr("invalid --max-services: %s (1..%u)", optarg, DEVICE_MAX_SERVICES_CEILING);
          return ARGS_ERR;
        }
        cfg->max_services = v;
        break;
      }
      case 1015: {
        static const enum_map_t map[] = {{"simple", 0}, {"main", 1}};
        int v;
        if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
          argerr("invalid --profile: %s (simple|main)", optarg);
          return ARGS_ERR;
        }
        cfg->rist_profile_main = v;
        profile_given = 1;
        break;
      }
      case 1016:
        if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_passphrase_in, sizeof cfg->srt_passphrase_in, optarg, "--srt-passphrase-in"))
          return ARGS_ERR;
        break;
      case 1017: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || (v != 16 && v != 24 && v != 32)) {
          argerr("invalid --srt-pbkeylen-in: %s (16|24|32)", optarg);
          return ARGS_ERR;
        }
        cfg->srt_pbkeylen_in = (int)v;
        break;
      }
      case 1018:
        if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_streamid_in, sizeof cfg->srt_streamid_in, optarg, "--srt-streamid-in"))
          return ARGS_ERR;
        break;
      case 1019:
        if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_packetfilter_in, sizeof cfg->srt_packetfilter_in, optarg, "--srt-packetfilter-in"))
          return ARGS_ERR;
        break;
      case 1020: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, 60000, &v)) {
          argerr("invalid --srt-latency-in: %s (1..60000 ms)", optarg);
          return ARGS_ERR;
        }
        cfg->srt_latency_in_ms = v;
        break;
      }
      case 1021:
        if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_passphrase, sizeof cfg->srt_passphrase, optarg, "--srt-passphrase"))
          return ARGS_ERR;
        break;
      case 1022: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || (v != 16 && v != 24 && v != 32)) {
          argerr("invalid --srt-pbkeylen: %s (16|24|32)", optarg);
          return ARGS_ERR;
        }
        cfg->srt_pbkeylen = (int)v;
        break;
      }
      case 1023:
        if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_streamid, sizeof cfg->srt_streamid, optarg, "--srt-streamid"))
          return ARGS_ERR;
        break;
      case 1024:
        if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_packetfilter, sizeof cfg->srt_packetfilter, optarg, "--srt-packetfilter"))
          return ARGS_ERR;
        break;
      case 1025: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, 60000, &v)) {
          argerr("invalid --srt-latency: %s (1..60000 ms)", optarg);
          return ARGS_ERR;
        }
        cfg->srt_latency_ms = v;
        break;
      }
      case 'h':
        print_help();
        return ARGS_HELP;
      default:
        return ARGS_ERR; /* getopt already reported */
    }
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  if (!have_input) {
    argerr("missing -i input");
    return ARGS_ERR;
  }
  if (!cfg->n_out) {
    argerr("missing -o output");
    return ARGS_ERR;
  }
  {
    int has_rtmps = 0;
    int has_rtmp = 0;
    int n_file = 0;
    for (int i = 0; i < cfg->n_out; i++) {
      if (cfg->out[i].kind == OUT_FILE) n_file++;
      if (cfg->out[i].kind == OUT_RTMPS) has_rtmps = 1;
      if (cfg->out[i].kind == OUT_RTMP || cfg->out[i].kind == OUT_RTMPS) has_rtmp = 1;
    }
    if ((cfg->format == FMT_MKV || cfg->format == FMT_MKA) && n_file != 1) {
      argerr("-f mkv/mka requires exactly one -o file target (plus optional rtmp(s) targets)");
      return ARGS_ERR;
    }
    if (cfg->insecure_tls && !has_rtmps && !cfg->unicast_emm_uri) log_line(TOOL_NAME ": --insecure needs -u or -o rtmps://");
    if (cfg->strip_lcevc && cfg->format == FMT_TS && !has_rtmp) log_line(TOOL_NAME ": --strip-lcevc has no effect, no -f mkv/mka or -o rtmp(s):// target");
  }
  if (cfg->biss2_sw_given && cfg->biss2_esw_given) {
    argerr("--biss2-sw and --biss2-esw are mutually exclusive");
    return ARGS_ERR;
  }
  if (cfg->biss2_esw_given && !have_biss_id) {
    argerr("--biss2-esw requires --biss2-id");
    return ARGS_ERR;
  }
  if (have_biss_id && !cfg->biss2_esw_given) {
    argerr("--biss2-id requires --biss2-esw");
    return ARGS_ERR;
  }
  if (cfg->biss1_sw_given && (cfg->biss2_sw_given || cfg->biss2_esw_given)) {
    argerr("--biss1-sw is mutually exclusive with --biss2-sw/--biss2-esw");
    return ARGS_ERR;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) return ARGS_ERR;
  if (profile_given && cfg->input.kind != INPUT_RIST)
    log_line(TOOL_NAME ": --profile needs -i rist://");
  if (argutil_srt_passphrase_opt(TOOL_NAME, cfg->srt_passphrase_in, "--srt-passphrase-in")) return ARGS_ERR;
  if (cfg->srt_pbkeylen_in && !cfg->srt_passphrase_in[0]) {
    argerr("--srt-pbkeylen-in requires --srt-passphrase-in");
    return ARGS_ERR;
  }
  if (cfg->input.kind != INPUT_SRT && (cfg->srt_passphrase_in[0] || cfg->srt_pbkeylen_in || cfg->srt_streamid_in[0] || cfg->srt_packetfilter_in[0] || cfg->srt_latency_in_ms))
    log_line(TOOL_NAME ": --srt-*-in needs -i srt://");
  if (argutil_srt_passphrase_opt(TOOL_NAME, cfg->srt_passphrase, "--srt-passphrase")) return ARGS_ERR;
  if (cfg->srt_pbkeylen && !cfg->srt_passphrase[0]) {
    argerr("--srt-pbkeylen requires --srt-passphrase");
    return ARGS_ERR;
  }
  {
    int has_srt_out = 0;
    for (int i = 0; i < cfg->n_out; i++) if (cfg->out[i].kind == OUT_SRT) has_srt_out = 1;
    if (!has_srt_out && (cfg->srt_passphrase[0] || cfg->srt_pbkeylen || cfg->srt_streamid[0] || cfg->srt_packetfilter[0] || cfg->srt_latency_ms))
      log_line(TOOL_NAME ": --srt-* needs -o srt://");
  }
  return ARGS_OK;
}
