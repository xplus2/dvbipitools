/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_SYS_IOUTIL_H
#define DVBIPITOOLS_LIB_SYS_IOUTIL_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* null-terminated, malloc'd. 0 ok, -1 error */
int read_all(FILE *f, char **out, size_t *out_len);
int read_path(const char *path, char **out, size_t *out_len);

uint32_t rand_seed32(void);

/* EINTR/EAGAIN-retrying pipe write, 100ms poll between EAGAIN spins.
   0 written fully, -1 unrecoverable (*stop set or EPIPE) */
int pipe_write_all(int fd, const unsigned char *buf, size_t n, const atomic_int *stop);

/* fgets that never splits a line. 1 line read, 0 EOF, -1 line >= cap bytes (rest of line discarded, buf unusable) */
int read_line(FILE *f, char *buf, size_t cap);

#endif
