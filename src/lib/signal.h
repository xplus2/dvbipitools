/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_SIGNAL_H
#define DIPIREC_SIGNAL_H

/* SIGINT/SIGTERM request stop; SIGPIPE ignored */
void signals_install(void);

/* nonzero once stop requested */
int signal_stop_requested(void);

#endif
