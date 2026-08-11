/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_CAPTURE_PRIV_H
#define DIPIFCCRET_CAPTURE_PRIV_H

#include "capture.h"

/* ranges.c: userspace whitelist check, used by frame.c regardless of the installed kernel filter */
int in_ranges(int family, const void *addr, const cidr_t *ranges, size_t range_count);

#endif
