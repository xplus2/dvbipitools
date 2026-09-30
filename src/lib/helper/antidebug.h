/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HELPER_ANTIDEBUG_H
#define DVBIPITOOLS_LIB_HELPER_ANTIDEBUG_H

/* no ptrace, no core dumps (release builds) */
void antidebug_install(void);

#endif
