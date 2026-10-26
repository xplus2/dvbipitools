/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>

#include "priv.h"
#include "../version.h"

void bim_print_help(void) {
  printf(
    "usage: %s -f xml -s <schema> [-i <path>] [-o <path>] [options]\n"
    "       %s -f bim -s <schema> [-i <path>] [-o <path>] [options]\n\n"
    "convert between XML and its BiM binary encoding (ISO/IEC 23001-1, 15938-1) through the XML Schema\n\n"
    "options:\n"
    "  -i, --input <path>     input path, - for stdin (default)\n"
    "  -o, --output <path>    output path, - for stdout (default)\n"
    "  -f, --format <fmt>     xml|bim - names the INPUT format\n"
    "  -s, --schema <path>    schema file, or directory searched for schemas by namespace. Repeatable.\n"
    "                         One file is used as is, of several the one declaring the namespace. The\n"
    "                         schemas a stream lists after the first are found the same way, and the\n"
    "                         encoders list the schemas the document uses besides the first\n"
    "      --profile <name>   dvb-tva|tva|sdns|generic (default dvb-tva)\n"
    "                           dvb-tva  TS 102 323 DVB-TVA-init and DVBBiMAccessUnit of TVAMain fragments\n"
    "                           tva      TS 102 822-3-2 access unit of TVAMain fragments, Zlib strings\n"
    "                           sdns     TS 102 034 SD&S segment, Zlib strings\n"
    "                           generic  ISO/IEC 23001-1 DecoderInit and one topmost element update\n"
    "      --syntax <name>    23001-1|15938-1 payload syntax (generic, default 23001-1)\n"
    "      --compat <list>    deviations of the MPEG-7 Systems conformance streams, comma separated:\n"
    "                           no-schema-mode  no SchemaModeUpdate with several schemas\n"
    "                           time-flags      time point and duration flags follow the derived type, sign flag first\n"
    "                           attr-local      attributes ordered by local name\n"
    "                           fixed-attr      optional fixed attributes coded (15938-1)\n"
    "                           list-string     IDREFS, NMTOKENS and ENTITIES as strings\n"
    "                           conformance     all of these\n"
    "      --len-mode <n>     lengthCodingMode 0|1|2 (generic, default 0)\n"
    "      --deferred         hasDeferredNodes (generic)\n"
    "      --cast             hasTypeCasting (generic)\n"
    "      --dvb-locator      optimized locator coding where representable (dvb-tva)\n"
    "      --dvb-duration     minute coding of durations where exact (dvb-tva)\n"
    "      --dvb-pubtime      PublishedTime coding of dateTime where representable (dvb-tva)\n"
    "      --zbuf-size <n>    Zlib BufferSize in bytes (tva, sdns), 0: 1000\n"
    "      --zgather          gather strings up to BufferSize per Zlib chunk\n"
    "  -c, --config <path>    yaml config, command line options override\n"
    "      --config-strict    unknown config keys are errors\n"
    "      --configtest       check the config and exit\n"
    "  -v, --verbose          progress on stderr\n"
    "      --color <when>     auto|always|never (default auto)\n"
    "  -h, --help             this help\n\n"
    "examples:\n"
    "  %s -f xml -s schemas/ -i guide.tva.xml -o guide.bim\n"
    "  %s -f bim -s schemas/ -i guide.bim -o guide.tva.xml\n",
    TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}
