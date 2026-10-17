/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_BIM_CODEC_CODEC_INT_H
#define LIB_BIM_CODEC_CODEC_INT_H

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib/sys/ioutil.h"
#include "lib/bim/codec.h"

#define DECODING_MODES 0x0F /* lengthCodingMode 00, hasDeferredNodes 0, hasTypeCasting 0, reserved 1111 */
#define MAX_NODES 100000
#define VALUE_MAX 4096

int node_put_attr(bim_node_t *n, const char *name, char *v);
int enum_bits(int n);

#endif
