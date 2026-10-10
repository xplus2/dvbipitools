/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_PSI_DVBTEXT_H
#define DVBIPITOOLS_LIB_DEMUX_PSI_DVBTEXT_H

#include <stddef.h>

/* DVB SI text (EN 300 468 Annex A) -> NUL-terminated UTF-8. returns bytes written, excl NUL.
   controls -> space, emphasis/C1 dropped, invalid/unmapped -> '?' */
size_t dvbtext_to_utf8(char *dst, size_t dstsz, const unsigned char *src, size_t len);

#endif
