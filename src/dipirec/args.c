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
#include <strings.h>

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

/* port 1..65535, digits only */
static int port_parse(const char *p, unsigned *out) {
  char *end;
  unsigned long v;
  if (*p == '\0')
    return -1;
  errno = 0;
  v = strtoul(p, &end, 10);
  if (errno || *end != '\0' || v == 0 || v > 65535)
    return -1;
  *out = (unsigned)v;
  return 0;
}

/* rest: [@]addr:port */
static int parse_direct(const char *rest, source_t *s) {
  const char *p = rest;
  char addr[64];

  if (*p == '@')
    p++;
  if (*p == '[') {
    const char *close = strchr(p, ']');
    size_t len;
    if (!close)
      return -1;
    len = (size_t)(close - (p + 1));
    if (len == 0 || len >= sizeof addr)
      return -1;
    memcpy(addr, p + 1, len);
    addr[len] = '\0';
    if (close[1] != ':' || port_parse(close + 2, &s->port))
      return -1;
    s->family = AF_INET6;
  } else {
    const char *colon = strrchr(p, ':');
    size_t len;
    if (!colon)
      return -1;
    len = (size_t)(colon - p);
    if (len == 0 || len >= sizeof addr)
      return -1;
    memcpy(addr, p, len);
    addr[len] = '\0';
    if (port_parse(colon + 1, &s->port))
      return -1;
    s->family = AF_INET;
  }

  /* multicast literal required */
  if (s->family == AF_INET) {
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

  strncpy(s->group, addr, sizeof s->group - 1);
  s->group[sizeof s->group - 1] = '\0';
  return 0;
}

/* [addr]:<port> or <addr4>:<port>, unicast, no multicast restriction (unlike parse_direct above) */
static int unicast_addr_port_parse(const char *s, int *family, char *addr_out, size_t addr_out_sz, unsigned *port_out) {
  char addr[64];
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
  } else {
    struct in6_addr a6;
    if (inet_pton(AF_INET6, addr, &a6) != 1)
      return -1;
  }

  if (strlen(addr) >= addr_out_sz)
    return -1;
  strcpy(addr_out, addr);
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
    if (port_parse(portbuf, &s->http_port))
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
  return -1;
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

typedef struct {
  const char *name;
  int value;
} enum_map_t;

static int map_lookup(const enum_map_t *m, size_t n, const char *s, int *out) {
  size_t i;
  for (i = 0; i < n; i++)
    if (strcmp(s, m[i].name) == 0) {
      *out = m[i].value;
      return 0;
    }
  return -1;
}

static int fmt_from_name(const char *s, out_fmt_t *f) {
  static const enum_map_t map[] = {{"raw", FMT_RAW}, {"ts", FMT_TS}, {"mkv", FMT_MKV}, {"mka", FMT_MKA}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], s, &v))
    return -1;
  *f = (out_fmt_t)v;
  return 0;
}

/* 1 if suffix gave format */
static int fmt_from_suffix(const char *path, out_fmt_t *f) {
  const char *dot = strrchr(path, '.');
  if (!dot)
    return 0;
  dot++;
  if (strcasecmp(dot, "ts") == 0) {
    *f = FMT_TS;
    return 1;
  }
  if (strcasecmp(dot, "mkv") == 0) {
    *f = FMT_MKV;
    return 1;
  }
  if (strcasecmp(dot, "mka") == 0) {
    *f = FMT_MKA;
    return 1;
  }
  return 0;
}

static void print_help(void) {
  printf(
      "usage: %s -i <uri> -o <path> [options]\n\n"
      "record a DVB-IPI stream to a file or stdout\n\n"
      "sources (-i):\n"
      "  rtp://@<group>:<port>              RTP wrapped SPTS multicast (@ "
      "optional)\n"
      "  udp://@<group>:<port>              raw SPTS multicast (@ optional)\n"
      "  http://<host>:<port>/<cmd>/<group>:<port>/   udpxy proxy (cmd = "
      "rtp|udp; also %% ~)\n"
      "  IPv6 groups in brackets, e.g. rtp://@[ff3e::1]:8208\n\n"
      "options:\n"
      "  -o, --out <path>       output file, or \"-\" for stdout\n"
      "  -i, --in <uri>         input source (see above)\n"
      "  -a, --audio <track>    audio track from 1, or \"all\" (default: all)\n"
      "  -f, --format <format>  raw|ts|mkv|mka (default: from -o suffix, else "
      "ts)\n"
      "  -s, --subtitles <mode> strip|keep|srt (srt: mkv/mka only; default: keep)\n"
      "  -t, --time <duration>  e.g. 90, 5m, 5m30s, 1h3m20s, 01:20:03, 10:20\n"
      "  -I, --iface <iface>   interface for the multicast join\n"
      "  -v, --verbose          periodic recording stats on stderr\n"
      "      --sub-lead <ms>    shift subtitles earlier (default 1000)\n"
      "      --color <when>     auto|always|never (default auto)\n"
      "      --ret <addr>:<port>   RET server unicast address (rtp:// only; enables gap repair)\n"
      "      --no-ret-mc        skip joining the RET server's multicast repair session\n"
      "      --ret-mc-port <port>  override the repair session port (default: -i's port)\n"
      "      --ret-pt <n>       RTX payload type, must match the RET server (default 99)\n"
      "      --ret-wait <ms>    hold budget after a NACK before giving up on a gap (default 200)\n"
      "  -h, --help             this help\n\n"
      "formats:\n"
      "  raw   unwrap RTP only, transport stream otherwise untouched\n"
      "  ts    single program transport stream; drops stuffing, NIT, EIT,\n"
      "        AIT and CA/EMM, keeps SDT, rewrites PAT/PMT\n"
      "  mkv   Matroska: H.264/HEVC video, AC3/EAC3/MP2/MP3/AAC/AAC-LATM audio\n"
      "  mka   same, audio only\n\n"
      "examples:\n"
      "  %s -i rtp://@239.2.24.1:8208 -o show.ts\n"
      "  %s -i rtp://@239.2.24.1:8208 -o show.mkv -s srt -t 1h30m -v\n"
      "  %s -i udp://@239.0.144.1:8208 -o radio.mka -I eth0\n"
      "  %s -i http://10.0.0.1:4022/rtp/239.2.24.1:8208 -o show.ts\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"out", required_argument, 0, 'o'},
      {"in", required_argument, 0, 'i'},
      {"audio", required_argument, 0, 'a'},
      {"format", required_argument, 0, 'f'},
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
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  const char *fmt_arg = NULL;
  const char *sub_arg = NULL;
  const char *time_arg = NULL;
  int have_in = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  cfg->audio_all = 1;
  cfg->subs = SUB_KEEP;
  cfg->sub_lead_ms = 1000;   /* teletext trails speech */
  cfg->ret.mc_enabled = 1;
  cfg->ret.rtx_pt = 99;
  cfg->ret.wait_ms = 200;
  optind = 1;
  while ((c = getopt_long(argc, argv, "o:i:a:f:s:t:I:vh", longopts, NULL)) !=
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
        static const enum_map_t map[] = {{"auto", 0}, {"always", 1}, {"never", 2}};
        int v;
        if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
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
        if (unicast_addr_port_parse(optarg, &cfg->ret.family, cfg->ret.addr, sizeof cfg->ret.addr, &cfg->ret.port)) {
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
  if (cfg->subs == SUB_SRT && cfg->format != FMT_MKV &&
      cfg->format != FMT_MKA) {
    argerr("-s srt requires -f mkv or mka");
    return ARGS_ERR;
  }
  return ARGS_OK;
}
