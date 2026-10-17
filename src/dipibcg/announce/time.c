/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dipibcg/announce.h"
#include "dipibcg/container.h"
#include "lib/bim/accessunit.h"
#include "lib/bim/bitwriter.h"
#include "lib/bim/strrepo.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/metrics/export.h"
#include "lib/net/announce_driver.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/net/netconnect.h"
#include "lib/sys/signal.h"
#include "lib/tva/bcg_doc.h"
#include "lib/tva/mapping.h"
#include "lib/tva/xmltv.h"
#include "dipibcg/version.h"
#include "dipibcg/wrapper.h"
#include "dipibcg/announce/announce_int.h"

int iso8601_to_minutes(const char *in, long *out) {
  iso8601_t f;
  if (iso8601_split(in, &f)) return -1;
  *out = date_to_mjd(f.y, f.mo, f.d) * 1440L + f.h * 60L + f.mi - f.off_min;
  return 0;
}

long now_minutes(void) {
  time_t t = time(NULL);
  struct tm tm;
  gmtime_r(&t, &tm);
  return date_to_mjd(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * 1440L + tm.tm_hour * 60L + tm.tm_min;
}

/* MJD epoch (date_to_mjd 1970-01-01) is day 40587 */
long minutes_to_unix(long mjd_minutes) {
  return (mjd_minutes - 40587L * 1440L) * 60L;
}
