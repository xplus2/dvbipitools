/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_SIGNAL_H
#define DIPIREC_SIGNAL_H

/* SIGINT/SIGTERM request stop; SIGHUP requests reload; SIGPIPE ignored */
void signals_install(void);

/* nonzero once stop requested */
int signal_stop_requested(void);

/* SIGHUP-triggered, clears on read (edge, not level) */
int signal_reload_requested(void);

/* CLOCK_MONOTONIC, seconds as a double */
double mono_seconds(void);

/* sleeps secs, in 100ms steps, checking signal_stop_requested() each step */
void sleep_interruptible(double secs);

#endif
