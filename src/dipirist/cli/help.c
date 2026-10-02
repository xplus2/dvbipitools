/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>

#include "priv.h"
#include "../version.h"

void rist_print_help(void) {
  printf(
  "usage: %s -i <uri> -o <uri> [options]\n\n"
  "bridge a DVB-IPI stream between plain RTP/UDP/file and a RIST link (VSF TR-06),\n"
  "either direction: exactly one of -i/-o must be rist://, the other is a regular\n"
  "dipirec-style endpoint\n\n"
  "endpoints:\n"
  "  rist://<host>:<port>[?params]  RIST peer, calls out; -o only\n"
  "  rist://@<host>:<port>[?params] RIST peer, listens; -i only\n"
  "                                 repeat -i/-o to bond several links\n"
  "                                 e.g. ?buffer=1000&secret=... - see --buffer/\n"
  "                                 --secret below for the equivalent flags)\n"
  "  rtp://@<group>:<port>          RTP wrapped SPTS multicast (@ optional)\n"
  "  udp://@<group>:<port>          raw SPTS multicast (@ optional)\n"
  "  http://<host>:<port>/<path>    HTTP TS stream, -i only\n"
  "  https://<host>:<port>/<path>   same, TLS (-k skips verification), -i only\n"
  "  -                              stdin (-i) or stdout (-o)\n"
  "  <path>                         a file\n"
  "  IPv6 groups in brackets, e.g. rtp://@[ff3e::1]:8700\n\n"
  "options:\n"
  "  -i, --in <uri>                 input (see above), repeatable if rist://\n"
  "  -o, --out <uri>                output (see above), repeatable if rist://\n"
  "  -I, --iface <iface>            interface for the non-RIST side's multicast join/send\n"
  "  -k, --insecure                 skip TLS verification, -i https:// only\n"
  "      --profile <name>           simple|main (default simple); main adds encryption\n"
  "      --secret <psk>             pre-shared key; requires --profile main\n"
  "      --encryption-type <bits>   AES key size, 128|256 requires --profile main\n"
  "      --cname <name>             RTCP cname; default library-generated\n"
  "      --buffer <ms>              RIST recovery buffer (min=max=<ms>); default library\n"
  "      --al-fec <L>:<D>           Annex E Layer 1 FEC (SMPTE 2022-1) on rtp:// L*D<=400, L<=40\n"
  "      --al-fec-port <port>       repair stream UDP port, requires --al-fec\n"
  "      --color <when>             auto|always|never (default auto)\n"
  "      --metrics <path>           socket for metrics (/run/dvbipitools/metrics.sock)\n"
  "      --metrics-id <name>        stable instance id; metrics disabled unless set\n"
  "      --metrics-interval <s>     snapshot interval in seconds (default: 5)\n"
  "      --metrics-inspect-ts <lvl> TS health metrics: off|basic|medium|full (default: off)\n"
  "  -v, --verbose                  periodic bridge stats on stderr\n"
  "  -d, --daemonize                fork to background after startup\n"
  "  -c, --config <path>            YAML config file (default: %s, if present)\n"
  "      --config-strict            fail on config file issues instead of warnings\n"
  "      --configtest               check the config file, then exit\n"
  "  -h, --help                     this help\n\n"
  "examples:\n"
  "  %s -i rtp://@239.1.1.1:5000 -o rist://1.2.3.4:6000 --buffer 1000\n"
  "  %s -i rist://@0.0.0.0:6000 -o rtp://@239.1.1.1:5000 --buffer 1000\n"
  "  %s -i rtp://@239.1.1.1:5000 -o rist://1.2.3.4:6000 -o rist://5.6.7.8:6000\n",
  TOOL_NAME, DEFAULT_CONFIG_PATH, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}
