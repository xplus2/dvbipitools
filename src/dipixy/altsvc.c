/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "altsvc.h"

#include <stdio.h>

static char g_value[48];
static char g_line[64];

void altsvc_set(unsigned port) {
  if (!port) {
    g_value[0] = '\0';
    g_line[0] = '\0';
    return;
  }
  snprintf(g_value, sizeof g_value, "h3=\":%u\"; ma=86400", port);
  snprintf(g_line, sizeof g_line, "Alt-Svc: %s\r\n", g_value);
}

const char *altsvc_h1_line(int is_tls) {
  return is_tls ? g_line : "";
}

const char *altsvc_value(void) {
  return g_value[0] ? g_value : NULL;
}
