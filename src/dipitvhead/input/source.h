/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_SOURCE_H
#define DIPITVHEAD_SOURCE_H

#include <stddef.h>
#include <sys/types.h>

#include "../args.h"

typedef struct tvsrc tvsrc_t;

/* opens per cfg->input (rtp/udp multicast, http(s), or stdin). NULL on failure (logged) */
tvsrc_t *tvsrc_open(const config_t *cfg);

/* TS bytes, RTP unwrapped if present. >0 len, 0 transient (retry), -1 hard error/EOF */
ssize_t tvsrc_read(tvsrc_t *s, unsigned char *buf, size_t cap);

void tvsrc_close(tvsrc_t *s);

#endif
