/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <signal.h>
#include <string.h>

#include "signal.h"

static volatile sig_atomic_t g_stop = 0;

static void on_stop(int sig) {
  (void)sig;
  g_stop = 1;
}

void signals_install(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_stop;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  signal(SIGPIPE, SIG_IGN); /* closed stdout pipe -> EPIPE */
}

int signal_stop_requested(void) { return g_stop; }
