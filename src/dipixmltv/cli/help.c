/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>

#include "priv.h"
#include "../version.h"

void xmltv_print_help(void) {
  printf(
    "usage: %s -f xmltv -M <map> [-i <path>] [-o <path>] [options]\n"
    "       %s -f tva [-R <revmap>] [-i <path>] [-o <path>] [options]\n"
    "       %s -S <scan.csv> [-i <guide.xml>] [-o <path>] [options]\n\n"
    "convert between xmltv and the DVB-IPI EPG (TVA) xml shape\n\n"
    "options:\n"
    "  -i, --input <path>       input path, - for stdin (default)\n"
    "  -o, --output <path>      output path, - for stdout (default)\n"
    "  -f, --format <fmt>       xmltv|tva - names the INPUT format\n"
    "  -M, --map <path>         xmltvid->uri,tsid,onid,sid (required for -f xmltv)\n"
    "  -R, --reverse-map <path> uri->preferred xmltv id (optional, -f tva only)\n"
    "  -S, --suggest-map <path> dipiscan csv. write a suggested -M mapping to -o,\n"
    "                           matched by channel name (review before use)\n"
    "  -v, --verbose            progress on stderr\n"
    "      --color <when>       auto|always|never (default auto)\n"
    "  -h, --help               this help\n\n"
    "examples:\n"
    "  %s -f xmltv -M mapping.csv -i guide.xml -o guide.tva.xml\n"
    "  %s -f tva -i guide.tva.xml -o guide.xml\n"
    "  %s -S scan.csv -i guide.xml -o mapping.csv\n",
    TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}
