/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_RIST_RISTLOG_H
#define DVBIPITOOLS_LIB_NET_RIST_RISTLOG_H

struct rist_logging_settings;

/* process-lifetime singleton: librist logs via global settings not per-ctx, set once.
   first verbose wins, never freed. NULL on fail or no librist.
   main-thread-only: rist ctx predates its own worker thread */
struct rist_logging_settings *ristlog_get(int verbose);

#endif
