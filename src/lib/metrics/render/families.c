/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/metrics/render/render_int.h"

const char *metric_kind_name(metric_kind_t k) {
  switch (k) {
    case M_COUNTER: return "counter";
    case M_INFO:    return "info";
    case M_GAUGE:
    case M_GAUGE_SIGNED: return "gauge";
  }
  return "gauge";
}

/* OpenMetrics family name: counter loses _total, info loses _info. sample names stay */
void add_family_name(dstrbuf_t *sb, const char *name, const char *type, int openmetrics) {
  size_t n = strlen(name);
  size_t cut = 0;
  char buf[96];
  if (openmetrics) {
    if (strcmp(type, "counter") == 0 && n > 6 && strcmp(name + n - 6, "_total") == 0) cut = 6;
    else if (strcmp(type, "info") == 0 && n > 5 && strcmp(name + n - 5, "_info") == 0) cut = 5;
  }
  snprintf(buf, sizeof buf, "%.*s", (int)(n - cut), name);
  dstrbuf_add(sb, buf);
}
