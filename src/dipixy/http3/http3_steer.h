/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_HTTP3_STEER_H
#define DIPIXY_HTTP3_STEER_H

#ifdef HAVE_HTTP3

#include <stddef.h>
#include <stdint.h>

/* reuseport BPF routing by 16-bit tag (CID start), tag = worker's group index */
#define H3_STEER_TAG_LEN 2

/* bracket worker's UDP binds, group index == call order */
void h3_steer_begin(void);

/* fd < 0 not bound. 0 success, -1 BPF attach failed */
int h3_steer_end(int fd4, int fd6);

/* attach steering program to reuseport group of fd */
int h3_steer_attach(int fd);

/* put worker tag in CID (>= H3_STEER_TAG_LEN bytes) */
void h3_steer_tag_cid(uint8_t *cid);

#endif /* HAVE_HTTP3 */

#endif /* DIPIXY_HTTP3_STEER_H */
