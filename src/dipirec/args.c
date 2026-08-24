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
#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/uriparse.h"

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
static int parse_mcast_addrport(const char *rest, int *family, char *group, size_t groupsz, unsigned *port) {
  if (*rest == '@')
    rest++;
  return uriparse_mcast_addrport(rest, family, group, groupsz, port);
}

static int parse_direct(const char *rest, source_t *s) {
  return parse_mcast_addrport(rest, &s->family, s->group, sizeof s->group, &s->port);
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
  if (strncmp(uri, "http://", 7) == 0 || strncmp(uri, "https://", 8) == 0) {
    s->kind = URI_HTTP;
    return http_url_parse(uri, &s->http);
  }
  if (strncmp(uri, "rist://", 7) == 0) {
    if (uri[7] != '@') /* rist:// as input always listens */
      return -1;
    if (strlen(uri) >= sizeof s->rist_uri)
      return -1;
    s->kind = URI_RIST;
    bufcpy(s->rist_uri, sizeof s->rist_uri, uri);
    return 0;
  }
  if (strncmp(uri, "srt://", 6) == 0) {
    const char *rest = uri + 6;
    int listen = *rest == '@';
    if (listen)
      rest++;
    if (argutil_addrport_parse(rest, &s->srt_family, s->srt_host, sizeof s->srt_host, &s->srt_port))
      return -1;
    s->kind = URI_SRT;
    s->srt_listen = listen;
    return 0;
  }
  if (strlen(uri) >= sizeof s->file_path)
    return -1;
  s->kind = URI_FILE;
  bufcpy(s->file_path, sizeof s->file_path, uri);
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
  case URI_HTTP:
    snprintf(buf, n, "%s://%s:%u%s", s->http.tls ? "https" : "http", s->http.host, s->http.port, s->http.path);
    break;
  case URI_FILE:
    snprintf(buf, n, "%s", s->file_path[0] ? s->file_path : "- (stdin)");
    break;
  case URI_RIST:
    snprintf(buf, n, "%s", s->rist_uri);
    break;
  case URI_SRT:
    if (s->srt_family == AF_INET6)
      snprintf(buf, n, "srt://%s[%s]:%u", s->srt_listen ? "@" : "", s->srt_host, s->srt_port);
    else
      snprintf(buf, n, "srt://%s%s:%u", s->srt_listen ? "@" : "", s->srt_host, s->srt_port);
    break;
  }
}

static int parse_out_uri(const char *uri, out_target_t *o) {
  memset(o, 0, sizeof *o);
  if (strncmp(uri, "rtp://", 6) == 0) {
    o->kind = OUT_RTP;
    return parse_mcast_addrport(uri + 6, &o->family, o->group, sizeof o->group, &o->port);
  }
  if (strncmp(uri, "udp://", 6) == 0) {
    o->kind = OUT_UDP;
    return parse_mcast_addrport(uri + 6, &o->family, o->group, sizeof o->group, &o->port);
  }
  if (strncmp(uri, "rist://", 7) == 0) {
    if (strlen(uri) >= sizeof o->rist_uri)
      return -1;
    o->kind = OUT_RIST;
    bufcpy(o->rist_uri, sizeof o->rist_uri, uri);
    return 0;
  }
  if (strncmp(uri, "srt://", 6) == 0) {
    if (uri[6] == '@') /* srt:// output always calls out, no listener mode */
      return -1;
    if (argutil_addrport_parse(uri + 6, &o->srt_family, o->srt_host, sizeof o->srt_host, &o->srt_port))
      return -1;
    o->kind = OUT_SRT;
    return 0;
  }
  {
    int r = uriparse_rtmp_or_file(uri, o->rtmp_url, sizeof o->rtmp_url, o->file_path, sizeof o->file_path);
    if (r < 0)
      return -1;
    o->kind = r == 2 ? OUT_RTMPS : r == 1 ? OUT_RTMP : OUT_FILE;
    return 0;
  }
}

void out_describe(const out_target_t *o, char *buf, size_t n) {
  switch (o->kind) {
  case OUT_RTP:
  case OUT_UDP: {
    const char *scheme = (o->kind == OUT_RTP) ? "rtp" : "udp";
    if (o->family == AF_INET6)
      snprintf(buf, n, "%s://@[%s]:%u", scheme, o->group, o->port);
    else
      snprintf(buf, n, "%s://@%s:%u", scheme, o->group, o->port);
    break;
  }
  case OUT_RIST:
    snprintf(buf, n, "%s", o->rist_uri);
    break;
  case OUT_RTMP:
  case OUT_RTMPS:
    snprintf(buf, n, "%s", o->rtmp_url);
    break;
  case OUT_FILE:
    snprintf(buf, n, "%s", strcmp(o->file_path, "-") == 0 ? "- (stdout)" : o->file_path);
    break;
  case OUT_SRT:
    if (o->srt_family == AF_INET6)
      snprintf(buf, n, "srt://[%s]:%u", o->srt_host, o->srt_port);
    else
      snprintf(buf, n, "srt://%s:%u", o->srt_host, o->srt_port);
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
      "usage: %s -i <uri> -o <target> [options]\n\n"
      "record a DVB-IPI stream to a file or stdout\n\n"
      "sources (-i):\n"
      "  rtp://@<group>:<port>    RTP wrapped SPTS multicast (@ optional)\n"
      "  udp://@<group>:<port>    raw SPTS multicast (@ optional)\n"
      "  http://<host>:<port>/<path>    HTTP TS stream\n"
      "  https://<host>:<port>/<path>   same, TLS (--insecure skips verification)\n"
      "  -                        stdin, TS or RTP-wrapped TS (auto-detected)\n"
      "  <path>                   a file, TS or RTP-wrapped TS (auto-detected)\n"
      "  rist://@<host>:<port>[?query]  RIST receiver, single peer (@ required,\n"
      "                           requires librist; no bonding, use dipirist for that)\n"
      "  srt://[@]<host>:<port>   SRT receiver, single peer (@ = listen, else calls\n"
      "                           out; requires libsrt; no bonding/rendezvous, use\n"
      "                           dipisrt for that)\n"
      "  IPv6 groups in brackets, e.g. rtp://@[ff3e::1]:8700\n\n"
      "outputs (-o, repeatable for multiple destinations at once), beyond a\n"
      "file path or \"-\" for stdout:\n"
      "  rtp://@<group>:<port>    RTP-wrapped multicast (-f raw|ts only)\n"
      "  udp://@<group>:<port>    raw multicast, no RTP header (-f raw|ts only)\n"
      "  rist://<host>:<port>[?query]  RIST sender, single peer (-f raw|ts only,\n"
      "                           requires librist)\n"
      "  srt://<host>:<port>      SRT sender, single peer per target, not bonded -\n"
      "                           repeat -o for more (-f raw|ts only, requires\n"
      "                           libsrt, always calls out, use dipisrt for bonding)\n"
      "  rtmp(s)://<host>[:port]/<app>/<key>  RTMP(S) publish, H.264/HEVC video,\n"
      "                           AC-3/E-AC-3/AAC audio\n\n"
      "options:\n"
      "  -o, --out <target>       output, repeatable: file, \"-\" for stdout, or\n"
      "                           rtp://udp://rist://srt://rtmp://rtmps:// (see below)\n"
      "  -i, --in <uri>           input source (see above)\n"
      "  -a, --audio <track>      audio track from 1, or \"all\" (default: all)\n"
      "  -f, --format <format>    raw|ts|mkv|mka (default: from -o suffix, else ts;\n"
      "                           mkv/mka rejected with a network -o; raw rejected\n"
      "                           alongside any -o rtmp://rtmps:// target)\n"
      "  -p, --pmt-pid <pid|all>  MPTS source only: pin one PMT pid, or record\n"
      "                           every program (\"all\"; rejected with -f mkv).\n"
      "                           ignored (warned) on an SPTS source. omitted on\n"
      "                           an MPTS source: fails early, lists programs\n"
      "  -s, --subtitles <mode>   strip|keep|srt (srt: mkv/mka only; default: keep)\n"
      "  -t, --time <duration>    e.g. 90, 5m, 5m30s, 1h3m20s, 01:20:03, 10:20\n"
      "  -I, --iface <iface>      interface for -i's multicast join\n"
      "  -O, --out-iface <iface>  interface for -o rtp://udp://'s multicast send\n"
      "      --ttl <n>            multicast TTL/hop-limit for -o rtp://udp:// (default: kernel, 1)\n"
      "      --profile <p>        simple|main; -o rist:// only (default: simple)\n"
      "      --secret <psk>       -o rist:// pre-shared key; requires --profile main\n"
      "      --cname <name>       -o rist:// cname (default: library default)\n"
      "      --buffer <ms>        -o rist:// recovery buffer (default: library default)\n"
      "      --profile-in <p>     simple|main; -i rist:// only (default: simple)\n"
      "      --srt-passphrase-in <pw>   passphrase for -i srt://, 10..79 chars\n"
      "      --srt-pbkeylen-in <n>      AES key length for --srt-passphrase-in: 16|24|32\n"
      "      --srt-streamid-in <id>     SRTO_STREAMID for -i srt://\n"
      "      --srt-packetfilter-in <c>  SRTO_PACKETFILTER for -i srt://\n"
      "      --srt-latency-in <ms>      SRTO_LATENCY for -i srt://\n"
      "      --srt-passphrase <pw>      passphrase for every -o srt:// target\n"
      "      --srt-pbkeylen <n>         AES key length for --srt-passphrase: 16|24|32\n"
      "      --srt-streamid <id>        SRTO_STREAMID for every -o srt:// target\n"
      "      --srt-packetfilter <c>     SRTO_PACKETFILTER for every -o srt:// target\n"
      "      --srt-latency <ms>         SRTO_LATENCY for every -o srt:// target\n"
      "      --insecure           skip TLS verification for -o rtmps://\n"
      "  -v, --verbose            periodic recording stats on stderr\n"
      "      --sub-lead <ms>      shift subtitles earlier (default 1000)\n"
      "      --color <when>       auto|always|never (default auto)\n"
      "      --metrics <path>     Unix datagram socket for metrics (default: /run/dvbipitools/metrics.sock)\n"
      "      --metrics-id <name>  stable instance id; metrics disabled unless set\n"
      "      --metrics-interval <s> snapshot interval in seconds (default: 5)\n"
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
      "  %s -i http://10.0.0.1:4022/rtp/239.19.75.1:8700 -o show.ts\n"
      "  %s -i show.ts --pace -o rtp://@239.9.9.9:6000 -O eth1 --ttl 16\n"
      "  %s -i rtp://@239.19.75.1:8700 -o rtmp://live.example.com/app/key\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
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
      {"out-iface", required_argument, 0, 'O'},
      {"ttl", required_argument, 0, 1010},
      {"profile", required_argument, 0, 1011},
      {"secret", required_argument, 0, 1012},
      {"cname", required_argument, 0, 1013},
      {"buffer", required_argument, 0, 1014},
      {"insecure", no_argument, 0, 1015},
      {"metrics", required_argument, 0, 1016},
      {"metrics-id", required_argument, 0, 1017},
      {"metrics-interval", required_argument, 0, 1018},
      {"profile-in", required_argument, 0, 1019},
      {"srt-passphrase-in", required_argument, 0, 1020},
      {"srt-pbkeylen-in", required_argument, 0, 1021},
      {"srt-streamid-in", required_argument, 0, 1022},
      {"srt-packetfilter-in", required_argument, 0, 1023},
      {"srt-latency-in", required_argument, 0, 1024},
      {"srt-passphrase", required_argument, 0, 1025},
      {"srt-pbkeylen", required_argument, 0, 1026},
      {"srt-streamid", required_argument, 0, 1027},
      {"srt-packetfilter", required_argument, 0, 1028},
      {"srt-latency", required_argument, 0, 1029},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  const char *fmt_arg = NULL;
  const char *sub_arg = NULL;
  const char *time_arg = NULL;
  const char *strip_arg = NULL;
  const char *profile_arg = NULL;
  const char *profile_in_arg = NULL;
  int have_in = 0;
  int have_secret = 0;
  int have_cname = 0;
  int have_buffer = 0;
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
  while ((c = getopt_long(argc, argv, "o:i:a:f:p:s:t:I:O:vh", longopts, NULL)) !=
         -1) {
    switch (c) {
      case 'o':
        if (cfg->n_out >= DIPIREC_MAX_OUT) {
          argerr("too many -o targets (max %d)", DIPIREC_MAX_OUT);
          return ARGS_ERR;
        }
        if (parse_out_uri(optarg, &cfg->out[cfg->n_out])) {
          argerr("invalid -o target: %s", optarg);
          return ARGS_ERR;
        }
        cfg->n_out++;
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
        cfg->iface_in = optarg;
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
      case 'O':
        cfg->iface_out = optarg;
        break;
      case 1010: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v > 255) {
          argerr("invalid --ttl: %s (0..255)", optarg);
          return ARGS_ERR;
        }
        cfg->out_ttl = (int)v;
        break;
      }
      case 1011:
        profile_arg = optarg;
        break;
      case 1012:
        if (bufcpy(cfg->rist_secret, sizeof cfg->rist_secret, optarg) >= sizeof cfg->rist_secret) {
          argerr("--secret too long");
          return ARGS_ERR;
        }
        have_secret = 1;
        break;
      case 1013:
        if (bufcpy(cfg->rist_cname, sizeof cfg->rist_cname, optarg) >= sizeof cfg->rist_cname) {
          argerr("--cname too long");
          return ARGS_ERR;
        }
        have_cname = 1;
        break;
      case 1014: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0) {
          argerr("invalid --buffer: %s (ms)", optarg);
          return ARGS_ERR;
        }
        cfg->rist_buffer_ms = (unsigned)v;
        have_buffer = 1;
        break;
      }
      case 1015:
        cfg->insecure_tls = 1;
        break;
      case 1016:
        cfg->metrics_sock = optarg;
        break;
      case 1017:
        cfg->metrics_id = optarg;
        break;
      case 1018: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 86400UL) {
          argerr("invalid --metrics-interval: %s (seconds, 1..86400)", optarg);
          return ARGS_ERR;
        }
        cfg->metrics_interval_s = (unsigned)v;
        break;
      }
      case 1019:
        profile_in_arg = optarg;
        break;
      case 1020:
        if (bufcpy(cfg->srt_passphrase_in, sizeof cfg->srt_passphrase_in, optarg) >= sizeof cfg->srt_passphrase_in) {
          argerr("--srt-passphrase-in too long");
          return ARGS_ERR;
        }
        break;
      case 1021: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || (v != 16 && v != 24 && v != 32)) {
          argerr("invalid --srt-pbkeylen-in: %s (16|24|32)", optarg);
          return ARGS_ERR;
        }
        cfg->srt_pbkeylen_in = (int)v;
        break;
      }
      case 1022:
        if (bufcpy(cfg->srt_streamid_in, sizeof cfg->srt_streamid_in, optarg) >= sizeof cfg->srt_streamid_in) {
          argerr("--srt-streamid-in too long");
          return ARGS_ERR;
        }
        break;
      case 1023:
        if (bufcpy(cfg->srt_packetfilter_in, sizeof cfg->srt_packetfilter_in, optarg) >= sizeof cfg->srt_packetfilter_in) {
          argerr("--srt-packetfilter-in too long");
          return ARGS_ERR;
        }
        break;
      case 1024: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 60000) {
          argerr("invalid --srt-latency-in: %s (1..60000 ms)", optarg);
          return ARGS_ERR;
        }
        cfg->srt_latency_in_ms = (unsigned)v;
        break;
      }
      case 1025:
        if (bufcpy(cfg->srt_passphrase, sizeof cfg->srt_passphrase, optarg) >= sizeof cfg->srt_passphrase) {
          argerr("--srt-passphrase too long");
          return ARGS_ERR;
        }
        break;
      case 1026: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || (v != 16 && v != 24 && v != 32)) {
          argerr("invalid --srt-pbkeylen: %s (16|24|32)", optarg);
          return ARGS_ERR;
        }
        cfg->srt_pbkeylen = (int)v;
        break;
      }
      case 1027:
        if (bufcpy(cfg->srt_streamid, sizeof cfg->srt_streamid, optarg) >= sizeof cfg->srt_streamid) {
          argerr("--srt-streamid too long");
          return ARGS_ERR;
        }
        break;
      case 1028:
        if (bufcpy(cfg->srt_packetfilter, sizeof cfg->srt_packetfilter, optarg) >= sizeof cfg->srt_packetfilter) {
          argerr("--srt-packetfilter too long");
          return ARGS_ERR;
        }
        break;
      case 1029: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 60000) {
          argerr("invalid --srt-latency: %s (1..60000 ms)", optarg);
          return ARGS_ERR;
        }
        cfg->srt_latency_ms = (unsigned)v;
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
  if (!cfg->n_out) {
    argerr("missing -o output");
    return ARGS_ERR;
  }
  if (!have_in) {
    argerr("missing -i input");
    return ARGS_ERR;
  }
  {
    int n_rist_out = 0;
    for (int i = 0; i < cfg->n_out; i++)
      if (cfg->out[i].kind == OUT_RIST)
        n_rist_out++;
    if (n_rist_out > 1) {
      argerr("at most one -o rist:// target: librist isn't safe with more than one context per process");
      return ARGS_ERR;
    }
    if (cfg->source.kind == URI_RIST && n_rist_out) {
      argerr("-i rist:// and -o rist:// cannot combine: librist isn't safe with more than one context per process");
      return ARGS_ERR;
    }
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
    for (int i = 0; i < cfg->n_out; i++)
      if (cfg->out[i].kind == OUT_FILE && strcmp(cfg->out[i].file_path, "-") != 0) {
        fmt_from_suffix(cfg->out[i].file_path, &cfg->format);
        break;
      }
  }
  {
    int has_rtp_udp = 0, has_rtmp = 0, has_rtmps = 0, has_non_file = 0, n_non_rtmp = 0;
    for (int i = 0; i < cfg->n_out; i++) {
      out_kind_t k = cfg->out[i].kind;
      if (k == OUT_RTP || k == OUT_UDP)
        has_rtp_udp = 1;
      if (k == OUT_RTMP || k == OUT_RTMPS) {
        has_rtmp = 1;
      } else {
        n_non_rtmp++;
        if (k != OUT_FILE)
          has_non_file = 1;
      }
      if (k == OUT_RTMPS)
        has_rtmps = 1;
    }
    if ((cfg->format == FMT_MKV || cfg->format == FMT_MKA) && (has_non_file || n_non_rtmp != 1)) {
      argerr("-f mkv/mka requires exactly one -o file target (plus optional rtmp(s) targets)");
      return ARGS_ERR;
    }
    if (cfg->format == FMT_RAW && has_rtmp) {
      argerr("-f raw is incompatible with an -o rtmp://rtmps:// target");
      return ARGS_ERR;
    }
    if (cfg->iface_out && !has_rtp_udp)
      log_line(TOOL_NAME ": --out-iface has no effect, no -o rtp:// or udp:// target");
    if (cfg->out_ttl && !has_rtp_udp)
      log_line(TOOL_NAME ": --ttl has no effect, no -o rtp:// or udp:// target");
    if (cfg->insecure_tls && !has_rtmps && !(cfg->source.kind == URI_HTTP && cfg->source.http.tls))
      log_line(TOOL_NAME ": --insecure has no effect, no -o rtmps:// target or -i https:// source");
  }
  if (strip_arg && cfg->format != FMT_TS)
    log_line(TOOL_NAME ": --strip has no effect outside -f ts");
  if (cfg->subs == SUB_SRT && cfg->format != FMT_MKV &&
      cfg->format != FMT_MKA) {
    argerr("-s srt requires -f mkv or mka");
    return ARGS_ERR;
  }
  if (profile_arg) {
    static const enum_map_t map[] = {{"simple", RIST_PROF_SIMPLE}, {"main", RIST_PROF_MAIN}};
    int v;
    if (map_lookup(map, sizeof map / sizeof map[0], profile_arg, &v)) {
      argerr("invalid --profile: %s (simple|main)", profile_arg);
      return ARGS_ERR;
    }
    cfg->rist_profile = (rist_profile_sel_t)v;
  }
  {
    int has_rist = 0;
    for (int i = 0; i < cfg->n_out; i++)
      if (cfg->out[i].kind == OUT_RIST)
        has_rist = 1;
    if (!has_rist && (profile_arg || have_secret || have_cname || have_buffer))
      log_line(TOOL_NAME ": --profile/--secret/--cname/--buffer have no effect, no -o rist:// target");
    if (has_rist && have_secret && cfg->rist_profile != RIST_PROF_MAIN) {
      argerr("--secret requires --profile main");
      return ARGS_ERR;
    }
  }
  if ((cfg->metrics_sock || cfg->metrics_interval_s) && !cfg->metrics_id) {
    argerr("--metrics/--metrics-interval require --metrics-id");
    return ARGS_ERR;
  }
  if (profile_in_arg) {
    static const enum_map_t map[] = {{"simple", RIST_PROF_SIMPLE}, {"main", RIST_PROF_MAIN}};
    int v;
    if (map_lookup(map, sizeof map / sizeof map[0], profile_in_arg, &v)) {
      argerr("invalid --profile-in: %s (simple|main)", profile_in_arg);
      return ARGS_ERR;
    }
    cfg->rist_profile_in = (rist_profile_sel_t)v;
  }
  if (profile_in_arg && cfg->source.kind != URI_RIST)
    log_line(TOOL_NAME ": --profile-in has no effect, no -i rist:// source");
  if (cfg->srt_passphrase_in[0] && (strlen(cfg->srt_passphrase_in) < 10 || strlen(cfg->srt_passphrase_in) > 79)) {
    argerr("--srt-passphrase-in must be 10..79 characters");
    return ARGS_ERR;
  }
  if (cfg->srt_pbkeylen_in && !cfg->srt_passphrase_in[0]) {
    argerr("--srt-pbkeylen-in requires --srt-passphrase-in");
    return ARGS_ERR;
  }
  if (cfg->source.kind != URI_SRT && (cfg->srt_passphrase_in[0] || cfg->srt_pbkeylen_in || cfg->srt_streamid_in[0] ||
                                       cfg->srt_packetfilter_in[0] || cfg->srt_latency_in_ms))
    log_line(TOOL_NAME ": --srt-*-in has no effect, no -i srt:// source");
  if (cfg->srt_passphrase[0] && (strlen(cfg->srt_passphrase) < 10 || strlen(cfg->srt_passphrase) > 79)) {
    argerr("--srt-passphrase must be 10..79 characters");
    return ARGS_ERR;
  }
  if (cfg->srt_pbkeylen && !cfg->srt_passphrase[0]) {
    argerr("--srt-pbkeylen requires --srt-passphrase");
    return ARGS_ERR;
  }
  {
    int has_srt_out = 0;
    for (int i = 0; i < cfg->n_out; i++)
      if (cfg->out[i].kind == OUT_SRT)
        has_srt_out = 1;
    if (!has_srt_out && (cfg->srt_passphrase[0] || cfg->srt_pbkeylen || cfg->srt_streamid[0] ||
                         cfg->srt_packetfilter[0] || cfg->srt_latency_ms))
      log_line(TOOL_NAME ": --srt-* has no effect, no -o srt:// target");
  }
  return ARGS_OK;
}
