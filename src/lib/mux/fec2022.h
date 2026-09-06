/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_FEC2022_H
#define DVBIPITOOLS_LIB_MUX_FEC2022_H

#include <stddef.h>
#include <stdint.h>

#define FEC2022_MAX_PKT (12 + 7 * 188)
#define FEC2022_MAX_REPAIR (12 + 16 + (FEC2022_MAX_PKT - 12))
#define FEC2022_MAX_L 40
#define FEC2022_MAX_LD 400

typedef struct fec2022_enc fec2022_enc_t;

fec2022_enc_t *fec2022_enc_new(unsigned l, unsigned d, unsigned char pt);
void fec2022_enc_free(fec2022_enc_t *e);

int fec2022_parse_ld(const char *s, unsigned *l_out, unsigned *d_out);

size_t fec2022_enc_feed(fec2022_enc_t *e, const unsigned char *pkt, size_t len, uint32_t pts_90k, unsigned char *out, size_t cap);

#endif
