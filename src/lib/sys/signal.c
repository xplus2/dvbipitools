/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>

#include "signal.h"

/* atomic, not just volatile sig_atomic_t: readers run on other threads now (CAS worker threads),
   and volatile alone gives no cross-thread ordering under C11 */
static atomic_int g_stop = 0;
static atomic_int g_reload = 0;
static atomic_int g_tpl_reload = 0;
static atomic_int g_tls_reload = 0;

static int g_wake_fd = -1;

static void on_stop(int sig) {
  (void)sig;
  atomic_store_explicit(&g_stop, 1, memory_order_relaxed);
  if (g_wake_fd >= 0) {
    uint64_t one = 1;
    ssize_t r = write(g_wake_fd, &one, sizeof one);
    (void)r;
  }
}

static void on_reload(int sig) {
  (void)sig;
  atomic_store_explicit(&g_reload, 1, memory_order_relaxed);
  atomic_store_explicit(&g_tpl_reload, 1, memory_order_relaxed);
}

static void on_tls_reload(int sig) {
  (void)sig;
  atomic_store_explicit(&g_tls_reload, 1, memory_order_relaxed);
}

void signals_install(void) {
  struct sigaction sa;
  g_wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_stop;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  sa.sa_handler = on_reload;
  sigaction(SIGHUP, &sa, NULL);
  sa.sa_handler = on_tls_reload;
  sigaction(SIGUSR1, &sa, NULL);
  signal(SIGPIPE, SIG_IGN); /* closed stdout pipe -> EPIPE */
}

int signal_stop_requested(void) { return atomic_load_explicit(&g_stop, memory_order_relaxed); }

int signal_reload_requested(void) { return atomic_exchange_explicit(&g_reload, 0, memory_order_relaxed); }

int signal_template_reload_requested(void) { return atomic_exchange_explicit(&g_tpl_reload, 0, memory_order_relaxed); }

int signal_tls_reload_requested(void) { return atomic_exchange_explicit(&g_tls_reload, 0, memory_order_relaxed); }

double mono_seconds(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

int signal_wake_fd(void) { return g_wake_fd; }

void sleep_interruptible(double secs) {
  double deadline = mono_seconds() + secs;
  int wfd = g_wake_fd;
  while (!signal_stop_requested()) {
    double remain = deadline - mono_seconds();
    if (remain <= 0.0) return;
    if (remain > 3600.0) remain = 3600.0;
    if (wfd >= 0) {
      struct pollfd pfd = {wfd, POLLIN, 0};
      poll(&pfd, 1, (int)(remain * 1000.0) + 1);
    } else {
      struct timespec req = {0, 100000000L};
      nanosleep(&req, NULL);
    }
  }
}
