/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/argutil.h"
#include "lib/log.h"

#include "args.h"
#include "filter/ts.h"
#include "version.h"

static void argerr(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void argerr(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  argutil_verr(TOOL_NAME, fmt, ap);
  va_end(ap);
}

/* rest: [@]addr:port, multicast literal required */
static int parse_direct(const char *rest, source_t *s) {
  const char *p = rest;

  if (*p == '@')
    p++;
  if (argutil_addrport_parse(p, &s->family, s->group, sizeof s->group, &s->port))
    return -1;

  if (s->family == AF_INET) {
    struct in_addr a;
    inet_pton(AF_INET, s->group, &a);
    if ((ntohl(a.s_addr) >> 28) != 0xE) /* 224.0.0.0/4 */
      return -1;
  } else {
    struct in6_addr a6;
    inet_pton(AF_INET6, s->group, &a6);
    if (a6.s6_addr[0] != 0xFF) /* ff00::/8 */
      return -1;
  }
  return 0;
}

/* rest: host[:port]/cmd/... */
static int parse_udpxy(const char *rest, source_t *s) {
  const char *p = rest;
  const char *seg, *segend;
  size_t len;

  if (*p == '[') {
    const char *close = strchr(p, ']');
    if (!close)
      return -1;
    len = (size_t)(close - (p + 1));
    if (len == 0 || len >= sizeof s->http_host)
      return -1;
    memcpy(s->http_host, p + 1, len);
    s->http_host[len] = '\0';
    p = close + 1;
  } else {
    const char *hp = p;
    while (*hp && *hp != ':' && *hp != '/')
      hp++;
    len = (size_t)(hp - p);
    if (len == 0 || len >= sizeof s->http_host)
      return -1;
    memcpy(s->http_host, p, len);
    s->http_host[len] = '\0';
    p = hp;
  }

  if (*p == ':') {
    const char *pe = ++p;
    char portbuf[6];
    while (isdigit((unsigned char)*pe))
      pe++;
    len = (size_t)(pe - p);
    if (len == 0 || len >= sizeof portbuf)
      return -1;
    memcpy(portbuf, p, len);
    portbuf[len] = '\0';
    if (argutil_port_parse(portbuf, &s->http_port))
      return -1;
    p = pe;
  } else {
    s->http_port = 80;
  }

  if (*p != '/')
    return -1;

  seg = p + 1;
  segend = strchr(seg, '/');
  len = segend ? (size_t)(segend - seg) : strlen(seg);
  if (len == 3 && memcmp(seg, "rtp", 3) == 0)
    s->rtp_wrapped = 1;
  else if (len == 3 && memcmp(seg, "udp", 3) == 0)
    s->rtp_wrapped = 0;
  else
    return -1;

  len = strlen(p);
  if (len >= sizeof s->http_path)
    return -1;
  memcpy(s->http_path, p, len + 1);
  return 0;
}

static int parse_uri(const char *uri, source_t *s) {
  memset(s, 0, sizeof *s);
  if (strcmp(uri, "-") == 0) {
    s->kind = URI_FILE; /* file_path[0] == '\0': stdin */
    return 0;
  }
  if (strncmp(uri, "rtp://", 6) == 0) {
    s->kind = URI_RTP;
    s->rtp_wrapped = 1;
    return parse_direct(uri + 6, s);
  }
  if (strncmp(uri, "udp://", 6) == 0) {
    s->kind = URI_UDP;
    s->rtp_wrapped = 0;
    return parse_direct(uri + 6, s);
  }
  if (strncmp(uri, "http://", 7) == 0) {
    s->kind = URI_UDPXY;
    return parse_udpxy(uri + 7, s);
  }
  if (strlen(uri) >= sizeof s->file_path)
    return -1;
  s->kind = URI_FILE;
  strcpy(s->file_path, uri);
  return 0;
}

void source_describe(const source_t *s, char *buf, size_t n) {
  switch (s->kind) {
  case URI_RTP:
  case URI_UDP: {
    const char *scheme = (s->kind == URI_RTP) ? "rtp" : "udp";
    if (s->family == AF_INET6)
      snprintf(buf, n, "%s://@[%s]:%u", scheme, s->group, s->port);
    else
      snprintf(buf, n, "%s://@%s:%u", scheme, s->group, s->port);
    break;
  }
  case URI_UDPXY:
    snprintf(buf, n, "http://%s:%u%s (%s)", s->http_host, s->http_port, s->http_path, s->rtp_wrapped ? "rtp" : "udp");
    break;
  case URI_FILE:
    snprintf(buf, n, "%s", s->file_path[0] ? s->file_path : "- (stdin)");
    break;
  }
}

long duration_parse(const char *s) {
  if (!s || !*s)
    return -1;

  if (strchr(s, ':')) {
    long parts[3];
    int n = 0;
    const char *p = s;
    long h = 0, m = 0, sec;
    for (;;) {
      char *end;
      long v;
      if (!isdigit((unsigned char)*p) || n >= 3)
        return -1;
      v = strtol(p, &end, 10);
      if (v < 0)
        return -1;
      parts[n++] = v;
      if (*end == '\0')
        break;
      if (*end != ':')
        return -1;
      p = end + 1;
    }
    if (n == 2) {
      m = parts[0];
      sec = parts[1];
    } else if (n == 3) {
      h = parts[0];
      m = parts[1];
      sec = parts[2];
    } else {
      return -1;
    }
    if (sec > 59 || (n == 3 && m > 59))
      return -1;
    h = h * 3600 + m * 60 + sec;
    return h > 0 ? h : -1;
  }

  if (strpbrk(s, "hms")) {
    const char *p = s;
    long total = 0;
    int last = 0; /* unit rank: h=1 m=2 s=3 */
    while (*p) {
      char *end;
      long v;
      int rank;
      if (!isdigit((unsigned char)*p))
        return -1;
      v = strtol(p, &end, 10);
      if (v < 0)
        return -1;
      switch (*end) {
      case 'h':
        rank = 1;
        total += v * 3600;
        break;
      case 'm':
        rank = 2;
        total += v * 60;
        break;
      case 's':
        rank = 3;
        total += v;
        break;
      default:
        return -1;
      }
      if (rank <= last) /* bad order or duplicate */
        return -1;
      last = rank;
      p = end + 1;
    }
    return total > 0 ? total : -1;
  }

  {
    char *end;
    long v = strtol(s, &end, 10);
    if (*end != '\0' || v <= 0)
      return -1;
    return v;
  }
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

/* comma-separated STRIP_* tokens, or "none".
   SDT/BAT: keep. hw receivers likely need SDT as much as PAT/PMT.
   TDT/TOT: both mean "drop pid 0x14" */
static int parse_strip(const char *s, config_t *cfg) {
  static const enum_map_t map[] = {
      {"NUL", STRIP_NUL}, {"NIT", STRIP_NIT}, {"AIT", STRIP_AIT}, {"EIT", STRIP_EIT},
      {"CAT", STRIP_CAT}, {"ECM", STRIP_ECM}, {"EMM", STRIP_EMM}, {"RST", STRIP_RST},
      {"TDT", STRIP_TDT}, {"TOT", STRIP_TOT}, {"INT", STRIP_INT}};
  unsigned mask = 0;
  const char *p = s;

  if (strcmp(s, "none") == 0) {
    cfg->strip_mask = 0;
    return 0;
  }
  while (*p) {
    const char *comma = strchr(p, ',');
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    char tok[8];
    int v;
    if (len == 0 || len >= sizeof tok)
      return -1;
    memcpy(tok, p, len);
    tok[len] = '\0';
    if (map_lookup(map, sizeof map / sizeof map[0], tok, &v))
      return -1;
    mask |= (unsigned)v;
    p += len;
    if (*p == ',')
      p++;
  }
  cfg->strip_mask = mask;
  return 0;
}

static int parse_audio(const char *s, config_t *cfg) {
  char *end;
  long v;

  if (strcmp(s, "all") == 0) {
    cfg->audio_all = 1;
    return 0;
  }
  v = strtol(s, &end, 10);
  if (*end != '\0' || v < 1 || v > 65535)
    return -1;
  cfg->audio_all = 0;
  cfg->audio_track = (unsigned)v;
  return 0;
}

static int fmt_from_name(const char *s, out_fmt_t *f) {
  static const enum_map_t map[] = {{"raw", FMT_RAW}, {"ts", FMT_TS}, {"mkv", FMT_MKV}, {"mka", FMT_MKA}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], s, &v))
    return -1;
  *f = (out_fmt_t)v;
  return 0;
}

/* 1 if suffix gave format. no "raw": not a meaningful file extension, name-only via -f */
static int fmt_from_suffix(const char *path, out_fmt_t *f) {
  static const enum_map_t map[] = {{"ts", FMT_TS}, {"mkv", FMT_MKV}, {"mka", FMT_MKA}};
  const char *dot = strrchr(path, '.');
  char lower[8];
  size_t i;
  int v;

  if (!dot)
    return 0;
  dot++;
  for (i = 0; i < sizeof lower - 1 && dot[i]; i++)
    lower[i] = (char)tolower((unsigned char)dot[i]);
  if (dot[i]) /* too long to be any known suffix */
    return 0;
  lower[i] = '\0';
  if (map_lookup(map, sizeof map / sizeof map[0], lower, &v))
    return 0;
  *f = (out_fmt_t)v;
  return 1;
}

static void print_help(void) {
  printf(
      "usage: %s -i <uri> -o <path> [options]\n\n"
      "record a DVB-IPI stream to a file or stdout\n\n"
      "sources (-i):\n"
      "  rtp://@<group>:<port>    RTP wrapped SPTS multicast (@ optional)\n"
      "  udp://@<group>:<port>    raw SPTS multicast (@ optional)\n"
      "  http://<host>:<port>/<cmd>/<group>:<port>/   udpxy proxy (cmd = rtp|udp; also %% ~)\n"
      "  -                        stdin, TS or RTP-wrapped TS (auto-detected)\n"
      "  <path>                   a file, TS or RTP-wrapped TS (auto-detected)\n"
      "  IPv6 groups in brackets, e.g. rtp://@[ff3e::1]:8700\n\n"
      "options:\n"
      "  -o, --out <path>         output file, or \"-\" for stdout\n"
      "  -i, --in <uri>           input source (see above)\n"
      "  -a, --audio <track>      audio track from 1, or \"all\" (default: all)\n"
      "  -f, --format <format>    raw|ts|mkv|mka (default: from -o suffix, else ts)\n"
      "  -p, --pmt-pid <pid|all>  MPTS source only: pin one PMT pid, or record\n"
      "                           every program (\"all\"; rejected with -f mkv).\n"
      "                           ignored (warned) on an SPTS source. omitted on\n"
      "                           an MPTS source: fails early, lists programs\n"
      "  -s, --subtitles <mode>   strip|keep|srt (srt: mkv/mka only; default: keep)\n"
      "  -t, --time <duration>    e.g. 90, 5m, 5m30s, 1h3m20s, 01:20:03, 10:20\n"
      "  -I, --iface <iface>      interface for the multicast join\n"
      "  -v, --verbose            periodic recording stats on stderr\n"
      "      --sub-lead <ms>      shift subtitles earlier (default 1000)\n"
      "      --color <when>       auto|always|never (default auto)\n"
      "      --ret <addr>:<port>  RET server unicast address (rtp:// only; enables gap repair)\n"
      "      --no-ret-mc          skip joining the RET server's multicast repair session\n"
      "      --ret-mc-port <port> override the repair session port (default: -i's port)\n"
      "      --ret-pt <n>         RTX payload type, must match the RET server (default 99)\n"
      "      --ret-wait <ms>      hold budget after a NACK before giving up on a gap (default 200)\n"
      "      --pace               file/stdin source only: pace output to the input's own\n"
      "                           timing (RTP timestamp if RTP-framed, else PCR)\n"
      "      --strip <list>       -f ts only: comma list of NUL,NIT,AIT,EIT,CAT,ECM,EMM,\n"
      "                           RST,TDT,TOT,INT to drop, or \"none\" (default: NUL,NIT,AIT,EIT)\n"
      "  -h, --help               this help\n\n"
      "formats:\n"
      "  raw   unwrap RTP only, transport stream otherwise untouched\n"
      "  ts    single program transport stream; drops stuffing, NIT, EIT,\n"
      "        AIT and CA/EMM, keeps SDT, rewrites PAT/PMT\n"
      "  mkv   Matroska: H.264/HEVC video, AC3/EAC3/MP2/MP3/AAC/AAC-LATM audio\n"
      "  mka   same, audio only\n\n"
      "examples:\n"
      "  %s -i rtp://@239.19.75.1:8700 -o show.ts\n"
      "  %s -i rtp://@239.19.75.1:8700 -o show.mkv -s srt -t 1h30m -v\n"
      "  %s -i udp://@239.0.175.1:8700 -o radio.mka -I eth0\n"
      "  %s -i http://10.0.0.1:4022/rtp/239.19.75.1:8700 -o show.ts\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"out", required_argument, 0, 'o'},
      {"in", required_argument, 0, 'i'},
      {"audio", required_argument, 0, 'a'},
      {"format", required_argument, 0, 'f'},
      {"pmt-pid", required_argument, 0, 'p'},
      {"subtitles", required_argument, 0, 's'},
      {"time", required_argument, 0, 't'},
      {"iface", required_argument, 0, 'I'},
      {"verbose", no_argument, 0, 'v'},
      {"sub-lead", required_argument, 0, 1000},
      {"color", required_argument, 0, 1001},
      {"ret", required_argument, 0, 1002},
      {"no-ret-mc", no_argument, 0, 1003},
      {"ret-mc-port", required_argument, 0, 1004},
      {"ret-pt", required_argument, 0, 1005},
      {"ret-wait", required_argument, 0, 1006},
      {"strip", required_argument, 0, 1007},
      {"pace", no_argument, 0, 1008},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  const char *fmt_arg = NULL;
  const char *sub_arg = NULL;
  const char *time_arg = NULL;
  const char *strip_arg = NULL;
  int have_in = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  cfg->audio_all = 1;
  cfg->subs = SUB_KEEP;
  cfg->sub_lead_ms = 1000;   /* teletext trails speech */
  cfg->ret.mc_enabled = 1;
  cfg->ret.rtx_pt = 99;
  cfg->ret.wait_ms = 200;
  cfg->strip_mask = STRIP_DEFAULT;
  optind = 1;
  while ((c = getopt_long(argc, argv, "o:i:a:f:p:s:t:I:vh", longopts, NULL)) !=
         -1) {
    switch (c) {
      case 'o':
        cfg->out_path = optarg;
        break;
      case 'i':
        if (parse_uri(optarg, &cfg->source)) {
          argerr("invalid -i uri: %s", optarg);
          return ARGS_ERR;
        }
        have_in = 1;
        break;
      case 'a':
        if (parse_audio(optarg, cfg)) {
          argerr("invalid -a track: %s (1..N or \"all\")", optarg);
          return ARGS_ERR;
        }
        break;
      case 'f':
        fmt_arg = optarg;
        break;
      case 'p':
        if (parse_pmt_sel(optarg, cfg)) {
          argerr("invalid -p pmt-pid: %s (0x0010..0x1FFE, or \"all\")", optarg);
          return ARGS_ERR;
        }
        break;
      case 's':
        sub_arg = optarg;
        break;
      case 't':
        time_arg = optarg;
        break;
      case 'I':
        cfg->iface = optarg;
        break;
      case 1001: {
        log_color_t v;
        if (log_color_from_string(optarg, &v)) {
          argerr("invalid --color: %s (auto|always|never)", optarg);
          return ARGS_ERR;
        }
        cfg->color_mode = v;
        break;
      }
    case 1000: {
      char *end;
      long v = strtol(optarg, &end, 10);
      if (*end != '\0' || v < 0 || v > 10000) {
        argerr("invalid --sub-lead: %s (0..10000 ms)", optarg);
        return ARGS_ERR;
      }
      cfg->sub_lead_ms = v;
      break;
    }
    case 'v':
        cfg->verbose = 1;
        break;
      case 1002:
        if (argutil_addrport_parse(optarg, &cfg->ret.family, cfg->ret.addr, sizeof cfg->ret.addr, &cfg->ret.port)) {
          argerr("invalid --ret addr:port: %s", optarg);
          return ARGS_ERR;
        }
        cfg->ret.enabled = 1;
        break;
      case 1003:
        cfg->ret.mc_enabled = 0;
        break;
      case 1004: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 65535) {
          argerr("invalid --ret-mc-port: %s", optarg);
          return ARGS_ERR;
        }
        cfg->ret.mc_port = (unsigned)v;
        break;
      }
      case 1005: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v > 127) {
          argerr("invalid --ret-pt: %s (0..127)", optarg);
          return ARGS_ERR;
        }
        cfg->ret.rtx_pt = (unsigned char)v;
        break;
      }
      case 1006: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0) {
          argerr("invalid --ret-wait: %s (ms)", optarg);
          return ARGS_ERR;
        }
        cfg->ret.wait_ms = (unsigned)v;
        break;
      }
      case 1007:
        strip_arg = optarg;
        break;
      case 1008:
        cfg->pace = 1;
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
  if (!cfg->out_path) {
    argerr("missing -o output");
    return ARGS_ERR;
  }
  if (!have_in) {
    argerr("missing -i input");
    return ARGS_ERR;
  }
  if (cfg->ret.enabled && cfg->source.kind != URI_RTP) {
    argerr("--ret requires -i rtp://, no RTP sequence numbers otherwise");
    return ARGS_ERR;
  }
  if (cfg->ret.enabled && cfg->ret.mc_enabled && cfg->ret.family != cfg->source.family) {
    argerr("--ret family must match -i's for the SSM repair join; use --no-ret-mc otherwise");
    return ARGS_ERR;
  }
  if (cfg->pace && cfg->source.kind != URI_FILE) {
    argerr("--pace requires -i - or -i <path>");
    return ARGS_ERR;
  }
  if (strip_arg && parse_strip(strip_arg, cfg)) {
    argerr("invalid --strip: %s (comma list of NUL,NIT,AIT,EIT,CAT,ECM,EMM,RST,TDT,TOT,INT, or \"none\")", strip_arg);
    return ARGS_ERR;
  }
  if (sub_arg) {
    static const enum_map_t map[] = {{"strip", SUB_STRIP}, {"keep", SUB_KEEP}, {"srt", SUB_SRT}};
    int v;
    if (map_lookup(map, sizeof map / sizeof map[0], sub_arg, &v)) {
      argerr("invalid -s: %s (strip|keep|srt)", sub_arg);
      return ARGS_ERR;
    }
    cfg->subs = (sub_mode_t)v;
  }
  if (time_arg) {
    long d = duration_parse(time_arg);
    if (d < 0) {
      argerr("invalid -t duration: %s", time_arg);
      return ARGS_ERR;
    }
    cfg->duration_s = d;
  }
  if (fmt_arg) {
    if (fmt_from_name(fmt_arg, &cfg->format)) {
      argerr("invalid -f format: %s (raw|ts|mkv|mka)", fmt_arg);
      return ARGS_ERR;
    }
  } else {
    cfg->format = FMT_TS;
    if (strcmp(cfg->out_path, "-") != 0)
      fmt_from_suffix(cfg->out_path, &cfg->format);
  }
  if (strip_arg && cfg->format != FMT_TS)
    log_line(TOOL_NAME ": --strip has no effect outside -f ts");
  if (cfg->subs == SUB_SRT && cfg->format != FMT_MKV &&
      cfg->format != FMT_MKA) {
    argerr("-s srt requires -f mkv or mka");
    return ARGS_ERR;
  }
  return ARGS_OK;
}
