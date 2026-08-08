/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* [@]<addr>:<port> or [@][<addr6>]:<port>, multicast literal required */
static int mcast_group_parse(const char *s, int *family, char *addr_out, size_t addr_out_sz, unsigned *port_out) {
  if (*s == '@')
    s++;
  if (argutil_addrport_parse(s, family, addr_out, addr_out_sz, port_out))
    return -1;

  if (*family == AF_INET) {
    struct in_addr a;
    inet_pton(AF_INET, addr_out, &a);
    if ((ntohl(a.s_addr) >> 28) != 0xE) /* 224.0.0.0/4 */
      return -1;
  } else {
    struct in6_addr a6;
    inet_pton(AF_INET6, addr_out, &a6);
    if (a6.s6_addr[0] != 0xFF) /* ff00::/8 */
      return -1;
  }
  return 0;
}

static int fmt_from_name(const char *s, out_fmt_t *f) {
  static const enum_map_t map[] = {{"ts", FMT_TS}, {"mkv", FMT_MKV}, {"mka", FMT_MKA}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], s, &v))
    return -1;
  *f = (out_fmt_t)v;
  return 0;
}

/* decimal or 0x-hex, PMT pid range 0x0010..0x1FFE */
static int pid_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 0);
  if (*end != '\0' || v < 0x0010 || v > 0x1FFE)
    return -1;
  *out = (unsigned)v;
  return 0;
}

static int parse_pmt_sel(const char *s, config_t *cfg) {
  if (strcmp(s, "all") == 0) {
    cfg->pmt_sel = PMT_SEL_ALL;
    return 0;
  }
  if (pid_parse(s, &cfg->pmt_pid))
    return -1;
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
  return -1;
}

void input_describe(const input_t *s, char *buf, size_t n) {
  switch (s->kind) {
  case INPUT_RTP:
  case INPUT_UDP: {
    const char *scheme = (s->kind == INPUT_RTP) ? "rtp" : "udp";
    if (s->family == AF_INET6)
      snprintf(buf, n, "%s://@[%s]:%u", scheme, s->group, s->port);
    else
      snprintf(buf, n, "%s://@%s:%u", scheme, s->group, s->port);
    break;
  }
  case INPUT_STDIN:
    snprintf(buf, n, "-");
    break;
  }
}

static void print_help(void) {
  printf(
      "usage: %s -i <uri> -k <keyfile> -s <serial> -e <emmfile> -o <output> [options]\n\n"
      "standalone CAS validation client: descrambles a dipitvhead-produced (or any\n"
      "wire-compatible) DVB-CSA2/CISSA transport stream, given the device's RSA\n"
      "private key - a client-side counterpart to dipitvhead's CAS muxer/scrambler.\n\n"
      "options:\n"
      "  %-27sudp://, rtp://, or \"-\" for stdin (required)\n"
      "  %-27sdevice RSA private key, PEM (required)\n"
      "  %-27sthis device's serial, matched against EMM-U addressing (required)\n"
      "  %-27sEMM cache: loaded on startup, rewritten on update (required)\n"
      "  %-27sunicast EMM pull endpoint, auth token as URI userinfo\n"
      "  %-27s(e.g. https://<token>@<host>:<port>/device/<serial>/emm)\n"
      "  %-27sskip TLS verification for -u/--unicast-emm (self-signed, hostname, expiry)\n"
      "  %-27sdescrambled output, file or \"-\" for stdout (required)\n"
      "  %-27sts|mkv|mka output container (default ts)\n"
      "  %-27sMPTS source only: pin one PMT pid, or descramble every program\n"
      "  %-27s(\"all\"; rejected with -f mkv). ignored (warned) on an SPTS\n"
      "  %-27ssource. omitted on an MPTS source: fails early, lists programs\n"
      "  %-27sincoming multicast interface\n"
      "  %-27speriodic stats + BK/SK/CW update lines on stderr\n"
      "  %-27sauto|always|never (default auto)\n"
      "  %-27sthis help\n\n"
      "example:\n"
      "  %s -i rtp://@239.0.0.1:1975 -k device.key -s e2e-01 -e emm.cache -o out.ts -v\n",
      TOOL_NAME,
      "-i, --input <uri>", "-k, --key <path>", "-s, --serial <id>", "-e, --emm-file <path>",
      "-u, --unicast-emm <uri>", "", "    --insecure",
      "-o, --output <path|->", "-f, --format <fmt>", "-p, --pmt-pid <pid|all>", "", "",
      "-I, --iface <iface>", "-v, --verbose",
      "    --color <when>", "-h, --help",
      TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"input", required_argument, 0, 'i'},
      {"key", required_argument, 0, 'k'},
      {"serial", required_argument, 0, 's'},
      {"emm-file", required_argument, 0, 'e'},
      {"unicast-emm", required_argument, 0, 'u'},
      {"insecure", no_argument, 0, 1003},
      {"output", required_argument, 0, 'o'},
      {"format", required_argument, 0, 'f'},
      {"pmt-pid", required_argument, 0, 'p'},
      {"iface", required_argument, 0, 'I'},
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1000},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_input = 0, have_key = 0, have_serial = 0, have_emm = 0, have_out = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  optind = 1;
  while ((c = getopt_long(argc, argv, "i:k:s:e:u:o:f:p:I:vh", longopts, NULL)) != -1) {
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
        have_key = 1;
        break;
      case 's':
        cfg->serial = optarg;
        have_serial = 1;
        break;
      case 'e':
        cfg->emm_file = optarg;
        have_emm = 1;
        break;
      case 'u':
        cfg->unicast_emm_uri = optarg;
        break;
      case 1003:
        cfg->insecure_tls = 1;
        break;
      case 'o':
        cfg->out_path = optarg;
        have_out = 1;
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
  if (!have_key) {
    argerr("missing -k device key");
    return ARGS_ERR;
  }
  if (!have_serial) {
    argerr("missing -s device serial");
    return ARGS_ERR;
  }
  if (!have_emm) {
    argerr("missing -e emm-file");
    return ARGS_ERR;
  }
  if (!have_out) {
    argerr("missing -o output");
    return ARGS_ERR;
  }
  return ARGS_OK;
}
