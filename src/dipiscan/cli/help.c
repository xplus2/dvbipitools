/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>

#include "priv.h"
#include "../version.h"

void scan_print_help(void) {
  printf(
    "usage: %s [options] 1>playlist 2>log\n\n"
    "sweep a multicast range for transport streams\n"
    "and write a playlist of what's discovered\n\n"
    "options:\n"
    "  -m, --mcast <addr>       base multicast group, v4 or v6; the last\n"
    "                           byte is swept 1..254                  [239.19.75.0]\n"
    "                           or <addr>/<prefixlen>, host range swept\n"
    "                           or <startaddr>-<stopaddr>, swept as given\n"
    "  -p, --port <port[-port]> port or inclusive port range          [8700]\n"
    "  -f, --format <fmt>       m3u|csv|xspf|xml|null                 [m3u]\n"
    "  -P, --provider <name>    DomainName (required on -f xml)\n"
    "  -o, --out <path>         output file, or \"-\" for stdout      [stdout]\n"
    "  -t, --timeout <secs>     wall-clock budget per candidate       [1]\n"
    "  -j, --jets <jets>        concurrent probing threads            [1]\n"
    "  -M, --mpts               report every program at an address,\n"
    "                           waits out the whole timeout budget per address\n"
    "  -u, --http-proxy <ip:port>  probe via an HTTP TS proxy instead of a\n"
    "                           direct IGMP/MLD join\n"
    "  -x, --http-path <tmpl>   proxy request path per candidate, -u only\n"
    "                           %%g=group %%p=port %%%%=literal %%    [/udp/%%g:%%p/]\n"
    "  -I, --iface <iface>      interface for the multicast join      [kernel default]\n"
    "  -v, --verbose            per-candidate diagnostics on stderr\n"
    "      --color <when>       auto|always|never                     [auto]\n"
    "  -c, --config <path>      YAML config file                      [%s, if present]\n"
    "      --config-strict      fail on config file issues instead of warnings\n"
    "      --configtest         check the config file, then exit\n"
    "  -h, --help               this help\n\n"
    "examples:\n"
    "  %s -m 239.19.75.0 -p 8700-8705 >hd.m3u\n"
    "  %s -v -f csv -o scan.csv\n"
    "  %s -u 127.0.0.1:8080 -m 239.19.75.0 -f xspf >playlist.xspf\n"
    "  %s -f xml -P example.org -o scan.xml    # feed straight into dipisds -a -i\n"
    "  %s -M -t 3 -f xml -P example.org -o scan.xml  # MPTS addresses too\n"
    "  %s -m 239.19.75.0/23 -p 8700-8705 >hd.m3u  # sweep a CIDR block\n"
    "  %s -m 239.19.75.10-239.19.75.20 >hd.m3u    # sweep an explicit range\n\n",
    TOOL_NAME, DEFAULT_CONFIG_PATH, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}
