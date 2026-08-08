/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <signal.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

#include "signal.h"

/* atomic, not just volatile sig_atomic_t: readers run on other threads now (CAS worker threads),
   and volatile alone gives no cross-thread ordering under C11 */
static atomic_int g_stop = 0;
static atomic_int g_reload = 0;

static void on_stop(int sig) {
  (void)sig;
  atomic_store_explicit(&g_stop, 1, memory_order_relaxed);
}

static void on_reload(int sig) {
  (void)sig;
  atomic_store_explicit(&g_reload, 1, memory_order_relaxed);
}

void signals_install(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_stop;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  sa.sa_handler = on_reload;
  sigaction(SIGHUP, &sa, NULL);
  signal(SIGPIPE, SIG_IGN); /* closed stdout pipe -> EPIPE */
}

int signal_stop_requested(void) { return atomic_load_explicit(&g_stop, memory_order_relaxed); }

int signal_reload_requested(void) { return atomic_exchange_explicit(&g_reload, 0, memory_order_relaxed); }

double mono_seconds(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

void sleep_interruptible(double secs) {
  double deadline = mono_seconds() + secs;
  while (mono_seconds() < deadline && !signal_stop_requested()) {
    struct timespec req = {0, 100000000L};
    nanosleep(&req, NULL);
  }
}
