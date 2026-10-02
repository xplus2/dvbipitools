/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>

#include "priv.h"
#include "../version.h"

void bim_print_help(void) {
  printf(
    "usage: %s -f xml [-i <path>] [-o <path>] [options]\n"
    "       %s -f bim [-i <path>] [-o <path>] [options]\n\n"
    "convert between the plain DVB-IPI EPG (TVA) xml shape and its BiM binary encoding\n\n"
    "options:\n"
    "  -i, --input <path>   input path, - for stdin (default)\n"
    "  -o, --output <path>  output path, - for stdout (default)\n"
    "  -f, --format <fmt>   xml|bim - names the INPUT format\n"
    "  -v, --verbose        progress on stderr\n"
    "      --color <when>   auto|always|never (default auto)\n"
    "  -h, --help           this help\n\n"
    "examples:\n"
    "  %s -f xml -i guide.tva.xml -o guide.bim\n"
    "  %s -f bim -i guide.bim -o guide.tva.xml\n",
    TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}
