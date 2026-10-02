/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>

#include "lib/metrics/protocol.h"

#include "priv.h"
#include "../version.h"

void metrics_print_help(void) {
  printf(
    "usage: %s [options]\n\n"
    "DVB-IPI headend metrics collector: receives snapshots from dvbipitools\n"
    "over a Unix datagram socket and serves them as OpenMetrics at GET /metrics\n\n"
    "options:\n"
    "  -S, --sock <path>        socket for snapshots on (default: %s)\n"
    "  -l, --listen <a>:<p>     HTTP listen address:port (default: %s:%u)\n"
    "      --tls-cert <path>    certificate file (PEM), HTTPS on -l, requires --tls-key\n"
    "      --tls-key <path>     private key file (PEM), requires --tls-cert\n"
    "      --auth <user>:<pass> HTTP Basic Auth for GET /metrics (default: off)\n"
    "  -e, --expiry <s>         drop an instance after this many seconds without a\n"
    "                           new snapshot (default: %d)\n"
    "  -v, --verbose            log rejected/dropped snapshots to stderr\n"
    "      --color <when>       auto|always|never (default auto)\n"
    "  -d, --daemonize          fork to background after startup, detach from terminal\n"
    "  -c, --config <path>      YAML config file (default: %s, if present)\n"
    "      --config-strict      fail on config file issues instead of warnings\n"
    "      --configtest         check the config file, then exit\n"
    "  -h, --help               this help\n\n"
    "example:\n"
    "  %s -l 0.0.0.0:9109\n"
    "  %s -l 0.0.0.0:9109 --tls-cert srv.crt --tls-key srv.key\n",
    TOOL_NAME, METRICS_DEFAULT_SOCK_PATH, DEFAULT_LISTEN_ADDR, (unsigned)DEFAULT_LISTEN_PORT, DEFAULT_EXPIRY_S, DEFAULT_CONFIG_PATH, TOOL_NAME, TOOL_NAME);
}
