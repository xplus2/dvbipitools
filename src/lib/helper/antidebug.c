/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "antidebug.h"

#ifdef NDEBUG
#include <sys/prctl.h>
#include <sys/resource.h>
#endif

void antidebug_install(void) {
#ifdef NDEBUG
  struct rlimit rl_core = {0, 0};
  prctl(PR_SET_DUMPABLE, 0);
  setrlimit(RLIMIT_CORE, &rl_core);
#endif
}
