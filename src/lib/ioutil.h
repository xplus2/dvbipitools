/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_IOUTIL_H
#define LIB_IOUTIL_H

#include <stddef.h>
#include <stdio.h>

/* null-terminated, malloc'd. 0 ok, -1 error */
int read_all(FILE *f, char **out, size_t *out_len);

#endif
