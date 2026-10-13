/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "time_ep.h"

#include "lib/sys/ioutil.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define FALLBACK_URL "https://time.akamai.com/?iso&ms"
#define HOST_MAX 127

size_t time_ep_render(char *buf, size_t n) {
  struct timespec ts;
  struct tm tm;
  int len;
  clock_gettime(CLOCK_REALTIME, &ts);
  gmtime_r(&ts.tv_sec, &tm);
  len = snprintf(buf, n, "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000L);
  return len > 0 && (size_t)len < n ? (size_t)len : 0;
}

static int host_ok(const char *host) {
  size_t len;
  if (!host) return 0;
  len = strlen(host);
  if (!len || len > HOST_MAX) return 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char ch = (unsigned char)host[i];
    if (!isalnum(ch) && ch != '.' && ch != '-' && ch != ':' && ch != '[' && ch != ']' && ch != '_') return 0;
  }
  return 1;
}

void time_ep_utc_url(char *out, size_t n, const char *cfg_url, int tls, const char *host) {
  if (cfg_url && cfg_url[0]) {
    bufcpy(out, n, cfg_url);
    return;
  }
  if (!host_ok(host)) {
    bufcpy(out, n, FALLBACK_URL);
    return;
  }
  snprintf(out, n, "%s://%s%s", tls ? "https" : "http", host, TIME_EP_PATH);
}
