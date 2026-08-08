/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/argutil.h"
#include "lib/log.h"

#include "args.h"
#include "version.h"

static void argerr(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void argerr(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  argutil_verr(TOOL_NAME, fmt, ap);
  va_end(ap);
}

/* comma-separated CIDR list (IPv4 or IPv6); light validation here, capture.c re-validates at BPF-build time */
static int ranges_parse(const char *s, config_t *cfg) {
  char buf[ARGS_MAX_RANGES * 64];
  char *tok, *save = NULL;
  if (strlen(s) >= sizeof buf)
    return -1;
  strcpy(buf, s);
  cfg->range_count = 0;
  for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
    char *slash = strchr(tok, '/');
    int is_v6 = strchr(tok, ':') != NULL;
    struct in_addr a4;
    struct in6_addr a6;
    long prefix;
    char *end;
    if (cfg->range_count >= ARGS_MAX_RANGES)
      return -1;
    if (!slash)
      return -1;
    *slash = '\0';
    if (is_v6 ? inet_pton(AF_INET6, tok, &a6) != 1 : inet_pton(AF_INET, tok, &a4) != 1)
      return -1;
    prefix = strtol(slash + 1, &end, 10);
    if (*end != '\0' || prefix < 0 || prefix > (is_v6 ? 128 : 32))
      return -1;
    *slash = '/';
    if (strlen(tok) >= sizeof cfg->ranges[0])
      return -1;
    strcpy(cfg->ranges[cfg->range_count], tok);
    cfg->range_ptrs[cfg->range_count] = cfg->ranges[cfg->range_count];
    cfg->range_count++;
  }
  return cfg->range_count ? 0 : -1;
}

static void print_help(void) {
  printf(
      "usage: %s -g <range> -l <addr>:<port> -I <iface> [options]\n\n"
      "RTP retransmission (RET, Annex F) and Fast Channel Change (FCC/RAMS, Annex I) server\n\n"
      "options:\n"
      "  -g, --range <cidr>[,<cidr>...]   multicast range(s) to capture, IPv4 or IPv6\n"
      "  -l, --listen <addr>:<port>       unicast bind, shared by RET and FCC traffic\n"
      "  -I, --iface <iface>              capture interface (required)\n"
      "  -M, --max-channels <n>           pre-allocated channel slots (default: 0 = 384)\n"
      "      --channel-idle-timeout <s>   free a channel slot after this many seconds with no\n"
      "                                   packets (default: 120, 0 = never reap)\n"
      "  -R, --rtx-pt <n>                 RTP payload type for retransmitted/burst packets (default: 99)\n"
      "  -w, --workers <n>                -l socket worker threads (default: 0 = online CPU cores)\n"
      "  -u, --user <user>                drop privileges to this user after opening the capture handle\n"
      "  -v, --verbose                    periodic stats on stderr\n"
      "      --color <when>               auto|always|never (default auto)\n"
      "  -h, --help                       this help\n\n"
      "RET (Annex F) options:\n"
      "      --no-ret                     disable RET entirely\n"
      "  -B, --buffer <ms>                per-channel retransmission buffer depth (default: 2000)\n"
      "  -F, --ff-port <port>             multicast RET session port (default: 0 = original channel's port)\n"
      "      --no-mc-ret                  disable the multicast RET session, unicast-only repair\n\n"
      "FCC (Annex I) options:\n"
      "      --no-fcc                     disable FCC entirely\n"
      "  -G, --gop-cap <ms>               safety cap on cached GOP-in-progress duration (default: 8000)\n"
      "  -C, --max-bursts <n>             pre-allocated concurrent burst-session slots (default: 4096)\n"
      "  -X, --burst-multiplier <n>       burst rate as multiple of observed nominal bitrate (default: 1.5)\n"
      "  -D, --burst-duration-cap <ms>    hard max burst duration regardless of signaling (default: 10000)\n\n"
      "example:\n"
      "  %s -g 239.0.0.0/8 -l 10.0.0.1:6000 -I eth0\n",
      TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"range", required_argument, 0, 'g'},
      {"listen", required_argument, 0, 'l'},
      {"iface", required_argument, 0, 'I'},
      {"max-channels", required_argument, 0, 'M'},
      {"channel-idle-timeout", required_argument, 0, 1005},
      {"rtx-pt", required_argument, 0, 'R'},
      {"workers", required_argument, 0, 'w'},
      {"user", required_argument, 0, 'u'},
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1002},
      {"no-ret", no_argument, 0, 1003},
      {"buffer", required_argument, 0, 'B'},
      {"ff-port", required_argument, 0, 'F'},
      {"no-mc-ret", no_argument, 0, 1001},
      {"no-fcc", no_argument, 0, 1004},
      {"gop-cap", required_argument, 0, 'G'},
      {"max-bursts", required_argument, 0, 'C'},
      {"burst-multiplier", required_argument, 0, 'X'},
      {"burst-duration-cap", required_argument, 0, 'D'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_range = 0, have_listen = 0, have_iface = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  cfg->buffer_ms = 2000;
  cfg->rtx_pt = 99;
  cfg->gop_cap_ms = 8000;
  cfg->max_bursts = 4096;
  cfg->burst_multiplier = 1.5;
  cfg->duration_cap_ms = 10000;
  cfg->channel_idle_timeout_s = 120;
  optind = 1;
  while ((c = getopt_long(argc, argv, "g:l:I:M:R:w:u:vhB:F:G:C:X:D:", longopts, NULL)) != -1) {
    switch (c) {
      case 'g':
        if (ranges_parse(optarg, cfg)) {
          argerr("invalid -g range: %s", optarg);
          return ARGS_ERR;
        }
        have_range = 1;
        break;
      case 'l':
        if (argutil_addrport_parse(optarg, &cfg->listen_family, cfg->listen_addr, sizeof cfg->listen_addr, &cfg->listen_port)) {
          argerr("invalid -l addr:port: %s", optarg);
          return ARGS_ERR;
        }
        have_listen = 1;
        break;
      case 'I':
        cfg->iface = optarg;
        have_iface = 1;
        break;
      case 'M': {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0') {
          argerr("invalid -M max-channels: %s", optarg);
          return ARGS_ERR;
        }
        cfg->max_channels = (size_t)v;
        break;
      }
      case 1005: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0') {
          argerr("invalid --channel-idle-timeout: %s (s)", optarg);
          return ARGS_ERR;
        }
        cfg->channel_idle_timeout_s = (unsigned)v;
        break;
      }
      case 'R': {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v > 127) {
          argerr("invalid -R rtx-pt: %s (0..127)", optarg);
          return ARGS_ERR;
        }
        cfg->rtx_pt = (unsigned char)v;
        break;
      }
      case 'w': {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0') {
          argerr("invalid -w workers: %s", optarg);
          return ARGS_ERR;
        }
        cfg->workers = (unsigned)v;
        break;
      }
      case 'u':
        cfg->user = optarg;
        break;
      case 'v':
        cfg->verbose = 1;
        break;
      case 1002: {
        log_color_t v;
        if (log_color_from_string(optarg, &v)) {
          argerr("invalid --color: %s (auto|always|never)", optarg);
          return ARGS_ERR;
        }
        cfg->color_mode = v;
        break;
      }
      case 1003:
        cfg->no_ret = 1;
        break;
      case 'B': {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0) {
          argerr("invalid -B buffer: %s (ms)", optarg);
          return ARGS_ERR;
        }
        cfg->buffer_ms = (unsigned)v;
        break;
      }
      case 'F': {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v > 65535) {
          argerr("invalid -F ff-port: %s", optarg);
          return ARGS_ERR;
        }
        cfg->ff_port = (unsigned)v;
        break;
      }
      case 1001:
        cfg->no_mc_ret = 1;
        break;
      case 1004:
        cfg->no_fcc = 1;
        break;
      case 'G': {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0) {
          argerr("invalid -G gop-cap: %s (ms)", optarg);
          return ARGS_ERR;
        }
        cfg->gop_cap_ms = (unsigned)v;
        break;
      }
      case 'C': {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0) {
          argerr("invalid -C max-bursts: %s", optarg);
          return ARGS_ERR;
        }
        cfg->max_bursts = (size_t)v;
        break;
      }
      case 'X': {
        char *end;
        double v = strtod(optarg, &end);
        if (*end != '\0' || v <= 1.0) {
          argerr("invalid -X burst-multiplier: %s (must be > 1.0)", optarg);
          return ARGS_ERR;
        }
        cfg->burst_multiplier = v;
        break;
      }
      case 'D': {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0) {
          argerr("invalid -D burst-duration-cap: %s (ms)", optarg);
          return ARGS_ERR;
        }
        cfg->duration_cap_ms = (unsigned)v;
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
  if (!have_range) {
    argerr("missing -g range");
    return ARGS_ERR;
  }
  if (!have_listen) {
    argerr("missing -l listen");
    return ARGS_ERR;
  }
  if (!have_iface) {
    argerr("missing -I iface");
    return ARGS_ERR;
  }
  if (cfg->no_ret && cfg->no_fcc) {
    argerr("--no-ret and --no-fcc together leave nothing to run");
    return ARGS_ERR;
  }
  if (cfg->workers == 0) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    cfg->workers = n > 0 ? (unsigned)n : 1;
  }
  return ARGS_OK;
}
