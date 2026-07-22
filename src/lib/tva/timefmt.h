/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXMLTV_TIMEFMT_H
#define DIPIXMLTV_TIMEFMT_H

#include <stddef.h>

/* "YYYYMMDDHHMMSS[ +HHMM]" -> "YYYY-MM-DDTHH:MM:SS[+HH:MM|Z]". 0 ok, -1 bad input */
int xmltv_time_to_iso8601(const char *in, char *out, size_t outcap);

/* "YYYY-MM-DDTHH:MM:SS[+HH:MM|Z]" -> "YYYYMMDDHHMMSS[ +HHMM]". 0 ok, -1 bad input */
int iso8601_to_xmltv_time(const char *in, char *out, size_t outcap);

#endif
