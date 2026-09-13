/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_ANTIDEBUG_H
#define LIB_ANTIDEBUG_H

/* no ptrace, no core dumps (release builds) */
void antidebug_install(void);

#endif
