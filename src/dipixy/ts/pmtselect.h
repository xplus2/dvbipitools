/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_PMTSELECT_H
#define DIPIXY_PMTSELECT_H

/* extracts "pmt=..." (dec or 0x-hex PID) from a raw HTTP query string.
   0 (auto: first PMT that resolves) if absent, unparsable, out of range
   (0..8191), or literally 0 */
unsigned pmt_select_parse_query(const char *query);

#endif
