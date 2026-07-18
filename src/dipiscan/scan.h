/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISCAN_SCAN_H
#define DIPISCAN_SCAN_H

#include <stdio.h>

#include "args.h"

/* 0 = done, 1 = stopped early by SIGINT/SIGTERM */
int scan_run(const config_t *cfg, FILE *out);

#endif
