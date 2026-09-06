/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_FEC2022_H
#define DVBIPITOOLS_LIB_DEMUX_FEC2022_H

#include <stddef.h>
#include <stdint.h>

#define FEC2022_MAX_PKT (12 + 7 * 188)
#define FEC2022_MAX_REPAIR (12 + 16 + (FEC2022_MAX_PKT - 12))
#define FEC2022_MAX_L 40
#define FEC2022_MAX_LD 400

typedef struct fec2022_dec fec2022_dec_t;

fec2022_dec_t *fec2022_dec_new(unsigned l, unsigned d);
void fec2022_dec_free(fec2022_dec_t *d);

int fec2022_dec_source(fec2022_dec_t *d, const unsigned char *pkt, size_t len);
void fec2022_dec_repair(fec2022_dec_t *d, const unsigned char *pkt, size_t len);
size_t fec2022_dec_drain(fec2022_dec_t *d, unsigned char *out, size_t cap);

#endif
