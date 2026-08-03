/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/log.h"

#include "args.h"
#include "version.h"

static void argerr(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void argerr(const char *fmt, ...) {
  va_list ap;
  fputs(TOOL_NAME ": ", stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}

static int port_parse(const char *p, unsigned *out) {
  char *end;
  unsigned long v;
  if (*p == '\0')
    return -1;
  v = strtoul(p, &end, 10);
  if (*end != '\0' || v == 0 || v > 65535)
    return -1;
  *out = (unsigned)v;
  return 0;
}

/* [@]<addr>:<port> or [@][<addr6>]:<port>, multicast literal required */
static int mcast_group_parse(const char *s, int *family, char *addr_out, size_t addr_out_sz, unsigned *port_out) {
  char addr[64];

  if (*s == '@')
    s++;
  if (*s == '[') {
    const char *close = strchr(s, ']');
    size_t len;
    if (!close)
      return -1;
    len = (size_t)(close - (s + 1));
    if (len == 0 || len >= sizeof addr)
      return -1;
    memcpy(addr, s + 1, len);
    addr[len] = '\0';
    if (close[1] != ':' || port_parse(close + 2, port_out))
      return -1;
    *family = AF_INET6;
  } else {
    const char *colon = strrchr(s, ':');
    size_t len;
    if (!colon)
      return -1;
    len = (size_t)(colon - s);
    if (len == 0 || len >= sizeof addr)
      return -1;
    memcpy(addr, s, len);
    addr[len] = '\0';
    if (port_parse(colon + 1, port_out))
      return -1;
    *family = AF_INET;
  }

  if (*family == AF_INET) {
    struct in_addr a;
    if (inet_pton(AF_INET, addr, &a) != 1)
      return -1;
    if ((ntohl(a.s_addr) >> 28) != 0xE) /* 224.0.0.0/4 */
      return -1;
  } else {
    struct in6_addr a6;
    if (inet_pton(AF_INET6, addr, &a6) != 1)
      return -1;
    if (a6.s6_addr[0] != 0xFF) /* ff00::/8 */
      return -1;
  }

  if (strlen(addr) >= addr_out_sz)
    return -1;
  strcpy(addr_out, addr);
  return 0;
}

static int fmt_from_name(const char *s, out_fmt_t *f) {
  if (!strcmp(s, "ts")) {
    *f = FMT_TS;
    return 0;
  }
  if (!strcmp(s, "mkv")) {
    *f = FMT_MKV;
    return 0;
  }
  if (!strcmp(s, "mka")) {
    *f = FMT_MKA;
    return 0;
  }
  return -1;
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
      "  %-27sincoming multicast interface\n"
      "  %-27speriodic stats + BK/SK/CW update lines on stderr\n"
      "  %-27sauto|always|never (default auto)\n"
      "  %-27sthis help\n\n"
      "example:\n"
      "  %s -i rtp://@239.0.0.1:1975 -k device.key -s e2e-01 -e emm.cache -o out.ts -v\n",
      TOOL_NAME,
      "-i, --input <uri>", "-k, --key <path>", "-s, --serial <id>", "-e, --emm-file <path>",
      "-u, --unicast-emm <uri>", "", "    --insecure",
      "-o, --output <path|->", "-f, --format <fmt>", "-I, --iface <iface>", "-v, --verbose",
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
      {"iface", required_argument, 0, 'I'},
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1000},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_input = 0, have_key = 0, have_serial = 0, have_emm = 0, have_out = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  optind = 1;
  while ((c = getopt_long(argc, argv, "i:k:s:e:u:o:f:I:vh", longopts, NULL)) != -1) {
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
      case 'I':
        cfg->iface_in = optarg;
        break;
      case 'v':
        cfg->verbose = 1;
        break;
      case 1000:
        if (!strcmp(optarg, "always"))
          cfg->color_mode = LOG_COLOR_ALWAYS;
        else if (!strcmp(optarg, "never"))
          cfg->color_mode = LOG_COLOR_NEVER;
        else if (!strcmp(optarg, "auto"))
          cfg->color_mode = LOG_COLOR_AUTO;
        else {
          argerr("invalid --color: %s (auto|always|never)", optarg);
          return ARGS_ERR;
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
