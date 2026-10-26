/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_DECODERINIT_PRIV_H
#define DVBIPITOOLS_LIB_BIM_DECODERINIT_PRIV_H

#include "lib/bim/decoderinit.h"

/* Amd 2: 9 flags + 24 ReservedBitsZero before PrefixTable */
#define FLAG_BITS 33

/* doc/error_codes.md */
#define BIM_DI_TRUNC "LE400291"
#define BIM_DI_ZERO "LE400292"
#define BIM_DI_RESERVED "LE400293"
#define BIM_DI_FLAGS "LE400294"
#define BIM_DI_WIDTH "LE400295"
#define BIM_DI_COUNTS "LE400296"
#define BIM_DI_LENGTH "LE400297"
#define BIM_DI_RANGE "LE400298"

#endif
