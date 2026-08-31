/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "pmtselect.h"
#include "pidfilter.h"

unsigned pmt_select_parse_query(const char *query) {
  char buf[16];
  char *end;
  unsigned long v;

  if (!query_param_extract(query, "pmt=", buf, sizeof buf))
    return 0;
  if (!pid_token_parse(buf, &end, &v) || v > 8191)
    return 0;
  return (unsigned)v;
}
