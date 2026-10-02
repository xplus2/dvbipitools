/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>

#include "priv.h"
#include "../version.h"

void bcg_print_help(void) {
  printf(
    "usage: %s -a -i <xmltv> -M <map.csv> -m <mcast>:<port> [options]\n"
    "       %s -l -m <mcast>:<port> [options]\n\n"
    "DVB-IPI EPG/BCG (ETSI TS 102 539): announce an xmltv guide on multicast as\n"
    "BiM-encoded TVA fragments, or listen for one and write xmltv\n\n"
    "options:\n"
    "  -a, --announce         headend mode: read -i, transmit on -m\n"
    "  -l, --listen           client mode: receive on -m, write -o\n"
    "  -m, --mcast <g>:<p>    multicast group:port ([addr6]:port for v6)\n"
    "  -I, --iface <iface>    multicast interface\n"
    "  -v, --verbose          periodic stats on stderr\n"
    "      --color <when>     auto|always|never (default auto)\n"
    "  -d, --daemonize        fork to background after startup, detach from terminal\n"
    "  -c, --config <path>    YAML config file (default: %s, if present)\n"
    "      --config-strict    fail on config file issues instead of warnings\n"
    "      --configtest       check the config file, then exit\n"
    "      --metrics <path>   socket for metrics (default: /run/dvbipitools/metrics.sock)\n"
    "      --metrics-id <id>  stable instance id; metrics disabled unless set\n"
    "      --metrics-interval <s> snapshot interval in seconds (default: 5)\n"
    "  -h, --help             this help\n\n"
    "listen mode options:\n"
    "  -t, --timeout <s>      stop after N seconds (default 35)\n"
    "  -o, --output <path>    xmltv output path, - for stdout (default)\n"
    "  -C, --csv-map <path>   also write a mapping csv (feeds back into -M)\n\n"
    "announce mode options:\n"
    "  -i, --input <path>     xmltv source (required)\n"
    "  -M, --map <path>       xmltv id -> uri,tsid,onid,sid csv (required)\n"
    "  -w, --window <hours>   only events starting within this (default 24)\n"
    "      --dscp <v>         output DSCP marking: video-high|video-low|voice|\n"
    "                         signalling|best-effort|0..63 (default: signalling)\n"
    "  -t, --interval <s>     repeat interval (default 5)\n"
    "  -Z, --compress         zlib-compress BCG containers (RFC 1950)\n\n"
    "examples:\n"
    "  %s -a -i guide.xml -M mapping.csv -m 239.255.0.2:3938\n"
    "  %s -l -m 239.255.0.2:3938 -o guide.xml -C mapping.csv\n",
    TOOL_NAME, TOOL_NAME, DEFAULT_CONFIG_PATH, TOOL_NAME, TOOL_NAME);
}
