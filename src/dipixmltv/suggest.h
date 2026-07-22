/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXMLTV_SUGGEST_H
#define DIPIXMLTV_SUGGEST_H

#include <stdio.h>

/* 0 ok, -1 error on stderr */
int suggest_map(FILE *xmltv_f, FILE *scan_f, FILE *out);

#endif
