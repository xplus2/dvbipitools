/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_TVA_TIMEFMT_H
#define DVBIPITOOLS_LIB_TVA_TIMEFMT_H

#include <stddef.h>

/* "YYYY[MM[DD[HH[MM[SS]]]]][ +HHMM|name]" -> "YYYY-MM-DDTHH:MM:SS[+HH:MM|Z]", missing fields 01/00.
   name: UT UTC GMT Z WET WEST BST CET CEST EET EEST.
   0 ok, 1 ok but unknown zone name dropped (time left without offset), -1 bad input */
int xmltv_time_to_iso8601(const char *in, char *out, size_t outcap);

/* "YYYY-MM-DDTHH:MM:SS[+HH:MM|Z]" -> "YYYYMMDDHHMMSS[ +HHMM]". 0 ok, -1 bad input */
int iso8601_to_xmltv_time(const char *in, char *out, size_t outcap);

#endif
