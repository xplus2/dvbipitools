/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* t_reactor_tid, defined in reactor.c: standalone definition for
   tests that link channels.c (RCU reader slot index) w/o reactor */

#include "reactor_tls.h"

_Thread_local int t_reactor_tid = -1;
