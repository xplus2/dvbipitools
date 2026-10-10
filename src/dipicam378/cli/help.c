/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>

#include "priv.h"
#include "../version.h"

void cam378_print_help(void) {
  printf(
    "usage: %s -k <keyfile> [options]\n\n"
    "cs378x (camd35/TCP) key server: holds a device's RSA private key, answers\n"
    "oscam's ECM/EMM with a control word - a software smartcard, nothing more.\n\n"
    "options:\n"
    "  -k, --key <path>           RSA private key, PEM (required)\n"
    "  -s, --serial <id>          device's serial, matched against EMM-U\n"
    "  -b, --bind <addr>          listen address, (default: %s)\n"
    "  -p, --port <n>             cs378x TCP listen port (default: %u)\n"
    "  -a, --auth [user:]<pass>   password must match the reader's \"password =\"\n"
    "                             (default: \"%s\") - its digest is the AES-128 key.\n"
    "      --caid <hex>           ECMs for any other CAID get a CMD08 (\"stop asking\")\n"
    "                             (optional, default: no CMD08 ever sent)\n"
    "      --algo <a>             cissa|csa2 (default: cissa)\n"
    "  -v, --verbose              protocol/decode detail on stderr\n"
    "      --color <when>         auto|always|never (default auto)\n"
    "      --metrics <path>       socket for metrics (default: /run/dvbipitools/metrics.sock)\n"
    "      --metrics-id <name>    stable instance id; metrics disabled unless set\n"
    "      --metrics-interval <s> snapshot interval in seconds (default: 5)\n"
    "  -d, --daemonize            fork to background after startup, detach from terminal\n"
    "  -c, --config <path>        YAML config file (default: %s, if present)\n"
    "      --config-strict        fail on config file issues instead of warnings\n"
    "      --configtest           check the config file, then exit\n"
    "  -h, --help                 this help\n\n"
    "example:\n"
    "  %s -k device.key -s e2e-01 -p %u\n",
    TOOL_NAME, ARGS_DEFAULT_BIND, ARGS_DEFAULT_PORT, ARGS_DEFAULT_PASSWORD, DEFAULT_CONFIG_PATH, TOOL_NAME, ARGS_DEFAULT_PORT);
}
